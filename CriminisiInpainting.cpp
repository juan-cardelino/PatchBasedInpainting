/*
Copyright (C) 2010 David Doria, daviddoria@gmail.com

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "CriminisiInpainting.h"

#include "RotateVectors.h"
#include "SelfPatchMatch.h"
#include "Helpers.h"

#include <iostream>
#include <iomanip> // setfill and setw

#include <vnl/vnl_double_2.h>

CriminisiInpainting::CriminisiInpainting()
{
  this->PatchRadius.Fill(5);

  this->BoundaryImage = UnsignedCharImageType::New();
  this->BoundaryNormals = VectorImageType::New();
  this->MeanDifferenceImage = FloatImageType::New();
  this->IsophoteImage = VectorImageType::New();
  this->PriorityImage = FloatImageType::New();
  this->Mask = UnsignedCharImageType::New();
  this->InputMask = UnsignedCharImageType::New();
  this->Image = ColorImageType::New();
  this->Patch = ColorImageType::New();
  this->ConfidenceImage = FloatImageType::New();

  this->WriteIntermediateImages = false;
}

void CriminisiInpainting::SetImage(ColorImageType::Pointer image)
{
  this->Image->Graft(image);
}

void CriminisiInpainting::SetInputMask(UnsignedCharImageType::Pointer mask)
{
  this->InputMask->Graft(mask);
}

void CriminisiInpainting::SetWriteIntermediateImages(bool flag)
{
  this->WriteIntermediateImages = flag;
}

void CriminisiInpainting::ExpandMask()
{
  // Ensure the mask is actually binary
  typedef itk::BinaryThresholdImageFilter <UnsignedCharImageType, UnsignedCharImageType>
          BinaryThresholdImageFilterType;

  BinaryThresholdImageFilterType::Pointer thresholdFilter
          = BinaryThresholdImageFilterType::New();
  thresholdFilter->SetInput(this->InputMask);
  thresholdFilter->SetLowerThreshold(122);
  thresholdFilter->SetUpperThreshold(255);
  thresholdFilter->SetInsideValue(255);
  thresholdFilter->SetOutsideValue(0);
  thresholdFilter->InPlaceOn();
  thresholdFilter->Update();
  
  // Expand the mask - this is necessary to prevent the isophotes from being undefined in the target region
  typedef itk::FlatStructuringElement<2> StructuringElementType;
  StructuringElementType::RadiusType radius;
  radius.Fill(this->PatchRadius[0]);
  //radius.Fill(2.0* this->PatchRadius[0]);

  StructuringElementType structuringElement = StructuringElementType::Box(radius);
  typedef itk::BinaryDilateImageFilter<UnsignedCharImageType, UnsignedCharImageType, StructuringElementType>
          BinaryDilateImageFilterType;

  BinaryDilateImageFilterType::Pointer expandMaskFilter
          = BinaryDilateImageFilterType::New();
  expandMaskFilter->SetInput(thresholdFilter->GetOutput());
  expandMaskFilter->SetKernel(structuringElement);
  expandMaskFilter->Update();
  this->Mask->Graft(expandMaskFilter->GetOutput());

  WriteScaledImage<UnsignedCharImageType>(this->Mask, "expandedMask.mhd");
}

void CriminisiInpainting::Initialize()
{
  ExpandMask();

  // Do this before we mask the image with the expanded mask
  ComputeIsophotes();

  // Create a blank priority image
  this->PriorityImage->SetRegions(this->Image->GetLargestPossibleRegion());
  this->PriorityImage->Allocate();

  // Create a blank mean difference image
  this->MeanDifferenceImage->SetRegions(this->Image->GetLargestPossibleRegion());
  this->MeanDifferenceImage->Allocate();

  // Clone the mask - we need to invert the mask to actually perform the masking, but we don't want to disturb the original mask
  typedef itk::ImageDuplicator< UnsignedCharImageType > DuplicatorType;
  DuplicatorType::Pointer duplicator = DuplicatorType::New();
  duplicator->SetInputImage(this->Mask);
  duplicator->Update();

  // Invert the mask
  typedef itk::InvertIntensityImageFilter <UnsignedCharImageType>
          InvertIntensityImageFilterType;

  InvertIntensityImageFilterType::Pointer invertIntensityFilter
          = InvertIntensityImageFilterType::New();
  invertIntensityFilter->SetInput(duplicator->GetOutput());
  invertIntensityFilter->InPlaceOn();
  invertIntensityFilter->Update();

  // Convert the inverted mask to floats and scale them to between 0 and 1
  // to serve as the initial confidence image
  typedef itk::RescaleIntensityImageFilter< UnsignedCharImageType, FloatImageType > RescaleFilterType;
  RescaleFilterType::Pointer rescaleFilter = RescaleFilterType::New();
  rescaleFilter->SetInput(invertIntensityFilter->GetOutput());
  rescaleFilter->SetOutputMinimum(0);
  rescaleFilter->SetOutputMaximum(1);
  rescaleFilter->Update();

  this->ConfidenceImage->Graft(rescaleFilter->GetOutput());
  WriteImage<FloatImageType>(this->ConfidenceImage, "InitialConfidence.mhd");

  // Mask the input image with the inverted mask (blank the region in the input image that we will fill in)
  typedef itk::MaskImageFilter< ColorImageType, UnsignedCharImageType, ColorImageType > MaskFilterType;
  MaskFilterType::Pointer maskFilter = MaskFilterType::New();
  maskFilter->SetInput1(this->Image);
  maskFilter->SetInput2(invertIntensityFilter->GetOutput());
  maskFilter->InPlaceOn();

  // We set non-masked pixels to green so we can more easily ensure these pixels are not being copied during the inpainting (since they are never supposed to be)
  ColorImageType::PixelType green;
  green[0] = 0;
  green[1] = 255;
  green[2] = 0;
  maskFilter->SetOutsideValue(green);
  maskFilter->Update();

  this->Image->Graft(maskFilter->GetOutput());

  // Debugging outputs
  WriteImage<ColorImageType>(this->Image, "InitialImage.mhd");
  WriteImage<UnsignedCharImageType>(this->Mask, "InitialMask.mhd");
  WriteImage<FloatImageType>(this->ConfidenceImage, "InitialConfidence.mhd");
  WriteImage<VectorImageType>(this->IsophoteImage, "InitialIsophotes.mhd");
}

void CriminisiInpainting::Inpaint()
{
  Initialize();

  int iteration = 0;
  while(HasMoreToInpaint(this->Mask))
    {
    std::cout << "Iteration: " << iteration << std::endl;

    FindBoundary();

    ComputeBoundaryNormals();

    ComputeAllPriorities();

    itk::Index<2> pixelToFill = FindHighestPriority(this->PriorityImage);
    std::cout << "Filling: " << pixelToFill << std::endl;

    itk::Index<2> bestMatchPixel = SelfPatchMatch<ColorImageType, UnsignedCharImageType>(this->Image, this->Mask, pixelToFill, this->PatchRadius[0]);

    // Extract the best patch
    typedef itk::RegionOfInterestImageFilter< ColorImageType,
                                              ColorImageType > ExtractFilterType;

    ExtractFilterType::Pointer extractFilter = ExtractFilterType::New();
    extractFilter->SetRegionOfInterest(GetRegionAroundPixel(bestMatchPixel, this->PatchRadius[0]));
    extractFilter->SetInput(this->Image);
    extractFilter->Update();
    this->Patch->Graft(extractFilter->GetOutput());

    CopyPatchIntoImage<ColorImageType>(this->Patch, this->Image, this->Mask, pixelToFill);

    UpdateConfidenceImage(bestMatchPixel, pixelToFill);
    UpdateIsophoteImage(bestMatchPixel, pixelToFill);

    // Update the mask
    UpdateMask(pixelToFill);

    // Sanity check everything
    DebugWriteAllImages(pixelToFill, iteration);
    //DebugTests();

    iteration++;
    } // end main while loop

  WriteImage<ColorImageType>(this->Image, "result.jpg");

}

void CriminisiInpainting::ComputeIsophotes()
{
  // Convert the color input image to a grayscale image
  UnsignedCharImageType::Pointer grayscaleImage = UnsignedCharImageType::New();
  ColorToGrayscale(this->Image, grayscaleImage);
  WriteImage<UnsignedCharImageType>(grayscaleImage, "greyscale.mhd");

  // Blur the image to compute better gradient estimates
  typedef itk::DiscreteGaussianImageFilter<
          UnsignedCharImageType, FloatImageType >  filterType;

  // Create and setup a Gaussian filter
  filterType::Pointer gaussianFilter = filterType::New();
  gaussianFilter->SetInput(grayscaleImage);
  gaussianFilter->SetVariance(2);
  gaussianFilter->Update();

  WriteImage<FloatImageType>(gaussianFilter->GetOutput(), "gaussianBlur.mhd");

  // Compute the gradient
  // TInputImage, TOperatorValueType, TOutputValueType
  typedef itk::GradientImageFilter<
      FloatImageType, float, float>  GradientFilterType;
  GradientFilterType::Pointer gradientFilter = GradientFilterType::New();
  gradientFilter->SetInput(gaussianFilter->GetOutput());
  gradientFilter->Update();

  WriteImage<VectorImageType>(gradientFilter->GetOutput(), "gradient.mhd");

  // Rotate the gradient 90 degrees to obtain isophotes from gradient
  typedef itk::UnaryFunctorImageFilter<VectorImageType, VectorImageType,
                                  RotateVectors<
    VectorImageType::PixelType,
    VectorImageType::PixelType> > FilterType;

  FilterType::Pointer rotateFilter = FilterType::New();
  rotateFilter->SetInput(gradientFilter->GetOutput());
  rotateFilter->Update();

  WriteImage<VectorImageType>(rotateFilter->GetOutput(), "originalIsophotes.mhd");

  // Mask the isophote image with the expanded version of the inpainting mask.
  // That is, keep only the values outside of the expanded mask. To do this, we have to first invert the mask.

  // Invert the mask
  typedef itk::InvertIntensityImageFilter <UnsignedCharImageType>
          InvertIntensityImageFilterType;

  InvertIntensityImageFilterType::Pointer invertMaskFilter
          = InvertIntensityImageFilterType::New();
  invertMaskFilter->SetInput(this->Mask);
  invertMaskFilter->Update();

  WriteScaledImage<UnsignedCharImageType>(invertMaskFilter->GetOutput(), "invertedExpandedMask.mhd");

  // Keep only values outside the masked region
  typedef itk::MaskImageFilter< VectorImageType, UnsignedCharImageType, VectorImageType > MaskFilterType;
  MaskFilterType::Pointer maskFilter = MaskFilterType::New();
  maskFilter->SetInput1(rotateFilter->GetOutput());
  maskFilter->SetInput2(invertMaskFilter->GetOutput());
  maskFilter->Update();

  this->IsophoteImage->Graft(maskFilter->GetOutput());
  WriteImage<VectorImageType>(this->IsophoteImage, "validIsophotes.mhd");
}

bool CriminisiInpainting::HasMoreToInpaint(UnsignedCharImageType::Pointer mask)
{
  itk::ImageRegionIterator<UnsignedCharImageType> imageIterator(mask,mask->GetLargestPossibleRegion());

  while(!imageIterator.IsAtEnd())
    {
    if(imageIterator.Get() != 0)
      {
      return true;
      }

    ++imageIterator;
    }

  return false;
}

void CriminisiInpainting::FindBoundary()
{
  /*
  // If we simply find the boundary of the mask, the isophotes will not be copied into these pixels because they are 1 pixel
  // away from the filled region
  typedef itk::BinaryContourImageFilter <UnsignedCharImageType, UnsignedCharImageType >
          binaryContourImageFilterType;

  binaryContourImageFilterType::Pointer binaryContourFilter
          = binaryContourImageFilterType::New ();
  binaryContourFilter->SetInput(this->Mask);
  binaryContourFilter->Update();

  this->BoundaryImage = binaryContourFilter->GetOutput();
  */

  // Instead, we have to invert the mask before finding the boundary

  // Invert the mask
  typedef itk::InvertIntensityImageFilter <UnsignedCharImageType>
          InvertIntensityImageFilterType;

  InvertIntensityImageFilterType::Pointer invertIntensityFilter
          = InvertIntensityImageFilterType::New();
  invertIntensityFilter->SetInput(this->Mask);
  invertIntensityFilter->Update();

  // Find the boundary
  typedef itk::BinaryContourImageFilter <UnsignedCharImageType, UnsignedCharImageType >
          binaryContourImageFilterType;

  binaryContourImageFilterType::Pointer binaryContourFilter
          = binaryContourImageFilterType::New ();
  binaryContourFilter->SetInput(invertIntensityFilter->GetOutput());
  binaryContourFilter->Update();

  //this->BoundaryImage = binaryContourFilter->GetOutput();
  this->BoundaryImage->Graft(binaryContourFilter->GetOutput());

  WriteImage<UnsignedCharImageType>(this->BoundaryImage, "Boundary.mhd");
}

void CriminisiInpainting::UpdateMask(UnsignedCharImageType::IndexType pixel)
{
  // Create a black patch
  UnsignedCharImageType::Pointer blackPatch = UnsignedCharImageType::New();
  CreateBlankPatch<UnsignedCharImageType>(blackPatch, this->PatchRadius[0]);

  pixel[0] -= blackPatch->GetLargestPossibleRegion().GetSize()[0]/2;
  pixel[1] -= blackPatch->GetLargestPossibleRegion().GetSize()[1]/2;

  // Paste it into the mask
  typedef itk::PasteImageFilter <UnsignedCharImageType, UnsignedCharImageType >
          PasteImageFilterType;

  PasteImageFilterType::Pointer pasteFilter
          = PasteImageFilterType::New();
  pasteFilter->SetInput(0, this->Mask);
  pasteFilter->SetInput(1, blackPatch);
  pasteFilter->SetSourceRegion(blackPatch->GetLargestPossibleRegion());
  pasteFilter->SetDestinationIndex(pixel);
  pasteFilter->InPlaceOn();
  pasteFilter->Update();

  this->Mask = pasteFilter->GetOutput();
}

void CriminisiInpainting::ComputeBoundaryNormals()
{
  // Blur the mask, compute the gradient, then keep the normals only at the original mask boundary

  typedef itk::DiscreteGaussianImageFilter<
          UnsignedCharImageType, FloatImageType >  filterType;

  // Create and setup a Gaussian filter
  filterType::Pointer gaussianFilter = filterType::New();
  gaussianFilter->SetInput(this->Mask);
  gaussianFilter->SetVariance(2);
  gaussianFilter->Update();

  typedef itk::GradientImageFilter<
      FloatImageType, float, float>  GradientFilterType;
  GradientFilterType::Pointer gradientFilter = GradientFilterType::New();
  gradientFilter->SetInput(gaussianFilter->GetOutput());
  gradientFilter->Update();

  // Only keep the normals at the boundary
  typedef itk::MaskImageFilter< VectorImageType, UnsignedCharImageType, VectorImageType > MaskFilterType;
  MaskFilterType::Pointer maskFilter = MaskFilterType::New();
  maskFilter->SetInput1(gradientFilter->GetOutput());
  maskFilter->SetInput2(this->BoundaryImage);
  maskFilter->Update();

  //this->BoundaryNormals = maskFilter->GetOutput();
  this->BoundaryNormals->Graft(maskFilter->GetOutput());

  // Normalize
  itk::ImageRegionIterator<VectorImageType> imageIterator(this->BoundaryNormals,this->BoundaryNormals->GetLargestPossibleRegion());
  itk::ImageRegionConstIterator<UnsignedCharImageType> boundaryIterator(this->BoundaryImage,this->BoundaryImage->GetLargestPossibleRegion());
  imageIterator.GoToBegin();
  boundaryIterator.GoToBegin();

  while(!imageIterator.IsAtEnd())
    {
    if(boundaryIterator.Get()) // The pixel is on the boundary
      {
      VectorImageType::PixelType p = imageIterator.Get();
      p.Normalize();
      imageIterator.Set(p);
      }
    ++imageIterator;
    ++boundaryIterator;
    }
}

itk::Index<2> CriminisiInpainting::FindHighestPriority(FloatImageType::Pointer priorityImage)
{
  typedef itk::MinimumMaximumImageCalculator <FloatImageType>
          ImageCalculatorFilterType;

  ImageCalculatorFilterType::Pointer imageCalculatorFilter
          = ImageCalculatorFilterType::New ();
  imageCalculatorFilter->SetImage(priorityImage);
  imageCalculatorFilter->Compute();

  return imageCalculatorFilter->GetIndexOfMaximum();
}

void CriminisiInpainting::ComputeAllPriorities()
{
  // Only compute priorities for pixels on the boundary
  itk::ImageRegionConstIterator<UnsignedCharImageType> boundaryIterator(this->BoundaryImage, this->BoundaryImage->GetLargestPossibleRegion());
  itk::ImageRegionIterator<FloatImageType> priorityIterator(this->PriorityImage, this->PriorityImage->GetLargestPossibleRegion());

  boundaryIterator.GoToBegin();
  priorityIterator.GoToBegin();

  // The main loop is over the boundary image. We only want to compute priorities at boundary pixels
  while(!boundaryIterator.IsAtEnd())
    {
    // If the pixel is not on the boundary, skip it and set its priority to -1.
    // -1 is used instead of 0 because if the priorities on the boundary get to 0, we still want to choose a boundary
    // point rather than a random image point.
    if(boundaryIterator.Get() == 0)
      {
      priorityIterator.Set(-1);
      ++boundaryIterator;
      ++priorityIterator;
      continue;
      }

    float priority = ComputePriority(boundaryIterator.GetIndex());
    //std::cout << "Priority: " << priority << std::endl;
    priorityIterator.Set(priority);

    ++boundaryIterator;
    ++priorityIterator;
    }
}

float CriminisiInpainting::ComputePriority(UnsignedCharImageType::IndexType queryPixel)
{
  double confidence = ComputeConfidenceTerm(queryPixel);
  double data = ComputeDataTerm(queryPixel);

  return confidence * data;
}


float CriminisiInpainting::ComputeConfidenceTerm(itk::Index<2> queryPixel)
{
  if(!this->Mask->GetLargestPossibleRegion().IsInside(GetRegionAroundPixel(queryPixel,this->PatchRadius[0])))
    {
    return 0;
    }

  itk::ImageRegionConstIterator<UnsignedCharImageType> maskIterator(this->Mask, GetRegionAroundPixel(queryPixel, this->PatchRadius[0]));
  itk::ImageRegionConstIterator<FloatImageType> confidenceIterator(this->ConfidenceImage, GetRegionAroundPixel(queryPixel, this->PatchRadius[0]));
  maskIterator.GoToBegin();
  confidenceIterator.GoToBegin();
  // confidence = sum of the confidences of patch pixels in the source region / area of the patch

  unsigned int numberOfPixels = GetNumberOfPixelsInPatch();

  float sum = 0;

  while(!maskIterator.IsAtEnd())
    {
    if(maskIterator.Get() == 0) // Pixel is in the source region
      {
      sum += confidenceIterator.Get();
      }
    ++confidenceIterator;
    ++maskIterator;
    }

  float areaOfPatch = static_cast<float>(numberOfPixels);
  return sum/areaOfPatch;
}

float CriminisiInpainting::ComputeDataTerm(UnsignedCharImageType::IndexType queryPixel)
{
  itk::CovariantVector<float, 2> isophote = this->IsophoteImage->GetPixel(queryPixel);
  itk::CovariantVector<float, 2> boundaryNormal = this->BoundaryNormals->GetPixel(queryPixel);

  double alpha = 255; // for grey scale images
  // D(p) = |dot(isophote direction at p, normal of the front at p)|/alpha

  vnl_double_2 vnlIsophote(isophote[0], isophote[1]);

  vnl_double_2 vnlNormal(boundaryNormal[0], boundaryNormal[1]);

  double dot = std::abs(dot_product(vnlIsophote,vnlNormal));

  return dot/alpha;
}

bool CriminisiInpainting::IsValidPatch(itk::Index<2> queryPixel, unsigned int radius)
{
  // Check if the patch is inside the image
  if(!this->Mask->GetLargestPossibleRegion().IsInside(GetRegionAroundPixel(queryPixel,radius)))
    {
    return false;
    }

  // Check if all the pixels in the patch are in the valid region of the image
  itk::ImageRegionConstIterator<UnsignedCharImageType> iterator(this->Mask,GetRegionAroundPixel(queryPixel, radius));
    while(!iterator.IsAtEnd())
    {
    if(iterator.Get()) // valid(unmasked) pixels are 0, masked pixels are 1 (non-zero)
      {
      return false;
      }

    ++iterator;
    }

  return true;
}

itk::CovariantVector<float, 2> CriminisiInpainting::GetAverageIsophote(UnsignedCharImageType::IndexType queryPixel)
{
  if(!this->Mask->GetLargestPossibleRegion().IsInside(GetRegionAroundPixel(queryPixel, this->PatchRadius[0])))
    {
    itk::CovariantVector<float, 2> v;
    v[0] = 0; v[1] = 0;
    return v;
    }

  itk::ImageRegionConstIterator<UnsignedCharImageType> iterator(this->Mask,GetRegionAroundPixel(queryPixel, this->PatchRadius[0]));

  std::vector<itk::CovariantVector<float, 2> > vectors;

  while(!iterator.IsAtEnd())
    {
    if(IsValidPatch(iterator.GetIndex(), 3))
      {
      vectors.push_back(this->IsophoteImage->GetPixel(iterator.GetIndex()));
      }

    ++iterator;
    }

  itk::CovariantVector<float, 2> averageVector;
  averageVector[0] = 0;
  averageVector[1] = 0;

  if(vectors.size() == 0)
    {
    return averageVector;
    }

  for(unsigned int i = 0; i < vectors.size(); i++)
    {
    averageVector[0] += vectors[i][0];
    averageVector[1] += vectors[i][1];
    }
  averageVector[0] /= vectors.size();
  averageVector[1] /= vectors.size();

  return averageVector;
}


void CriminisiInpainting::UpdateConfidenceImage(FloatImageType::IndexType sourcePixel, FloatImageType::IndexType targetPixel)
{
  // Loop over all pixels in the target region which were just filled and update their confidence.
  itk::ImageRegionIterator<FloatImageType> confidenceIterator(this->ConfidenceImage, GetRegionAroundPixel(targetPixel, this->PatchRadius[0]));
  itk::ImageRegionIterator<UnsignedCharImageType> maskIterator(this->Mask, GetRegionAroundPixel(targetPixel, this->PatchRadius[0]));
  confidenceIterator.GoToBegin();
  maskIterator.GoToBegin();

  while(!maskIterator.IsAtEnd())
    {
    if(maskIterator.Get()) // This was a target pixel, compute its confidence
      {
      confidenceIterator.Set(ComputeConfidenceTerm(maskIterator.GetIndex()));
      }
    ++confidenceIterator;
    ++maskIterator;
    }
}

void CriminisiInpainting::UpdateIsophoteImage(FloatImageType::IndexType sourcePixel, itk::Index<2> targetPixel)
{
  // Copy isophotes from best patch
  typedef itk::RegionOfInterestImageFilter< VectorImageType,
                                            VectorImageType > ExtractFilterType;

  ExtractFilterType::Pointer extractFilter = ExtractFilterType::New();
  extractFilter->SetRegionOfInterest(GetRegionAroundPixel(sourcePixel, this->PatchRadius[0]));
  extractFilter->SetInput(this->IsophoteImage);
  extractFilter->Update();

  VectorImageType::Pointer isophoteImagePatch = extractFilter->GetOutput();

  itk::ImageRegionIterator<VectorImageType> imageIterator(isophoteImagePatch, isophoteImagePatch->GetLargestPossibleRegion());
  itk::ImageRegionIterator<UnsignedCharImageType> maskIterator(this->Mask, GetRegionAroundPixel(sourcePixel, this->PatchRadius[0]));
  imageIterator.GoToBegin();
  maskIterator.GoToBegin();

  // "clear" the pixels which are in the target region.
  VectorImageType::PixelType blankPixel;
  blankPixel.Fill(0);

  while(!imageIterator.IsAtEnd())
  {
    if(maskIterator.Get() != 0) // we are in the target region
      {
      imageIterator.Set(blankPixel);
      }

    ++imageIterator;
    ++maskIterator;
  }

  CopyPatchIntoImage<VectorImageType>(isophoteImagePatch, this->IsophoteImage, this->Mask, targetPixel);
}

void CriminisiInpainting::ColorToGrayscale(ColorImageType::Pointer colorImage, UnsignedCharImageType::Pointer grayscaleImage)
{
  grayscaleImage->SetRegions(colorImage->GetLargestPossibleRegion());
  grayscaleImage->Allocate();

  itk::ImageRegionConstIterator<ColorImageType> colorImageIterator(colorImage,colorImage->GetLargestPossibleRegion());
  itk::ImageRegionIterator<UnsignedCharImageType> grayscaleImageIterator(grayscaleImage,grayscaleImage->GetLargestPossibleRegion());

  ColorImageType::PixelType largestPixel;
  largestPixel[0] = 255;
  largestPixel[1] = 255;
  largestPixel[2] = 255;

  float largestNorm = largestPixel.GetNorm();

  while(!colorImageIterator.IsAtEnd())
  {
    grayscaleImageIterator.Set(colorImageIterator.Get().GetNorm()*(255./largestNorm));

    ++colorImageIterator;
    ++grayscaleImageIterator;
  }
}

unsigned int CriminisiInpainting::GetNumberOfPixelsInPatch()
{
  return this->GetPatchSize()[0]*this->GetPatchSize()[1];
}


itk::Size<2> CriminisiInpainting::GetPatchSize()
{
  itk::Size<2> patchSize;

  patchSize[0] = (this->PatchRadius[0]*2)+1;
  patchSize[1] = (this->PatchRadius[1]*2)+1;

  return patchSize;
}

void CriminisiInpainting::DebugTests()
{
  // Check to make sure patch doesn't have any masked pixels
  itk::ImageRegionIterator<ColorImageType> patchIterator(this->Patch,this->Patch->GetLargestPossibleRegion());

  while(!patchIterator.IsAtEnd())
    {
    ColorImageType::PixelType val = patchIterator.Get();
    if(val[0] == 0 && val[1] == 255 && val[2] == 0)
      {
      std::cerr << "Patch has a blank pixel!" << std::endl;
      exit(-1);
      }
    ++patchIterator;
    }
}

template <class T>
void CriminisiInpainting::WriteScaledImage(typename T::Pointer image, std::string filename)
{
  typedef itk::RescaleIntensityImageFilter<T, UnsignedCharImageType> RescaleFilterType; // expected ';' before rescaleFilter

  typename RescaleFilterType::Pointer rescaleFilter = RescaleFilterType::New();
  rescaleFilter->SetInput(image);
  rescaleFilter->SetOutputMinimum(0);
  rescaleFilter->SetOutputMaximum(255);
  rescaleFilter->Update();

  typename itk::ImageFileWriter<T>::Pointer writer = itk::ImageFileWriter<T>::New();
  writer->SetFileName(filename);
  writer->SetInput(image);
  writer->Update();
}
