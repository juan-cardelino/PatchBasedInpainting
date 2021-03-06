/*=========================================================================
 *
 *  Copyright David Doria 2012 daviddoria@gmail.com
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *         http://www.apache.org/licenses/LICENSE-2.0.txt
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *=========================================================================*/

#ifndef InpaintingIntroducedEnergy_HPP
#define InpaintingIntroducedEnergy_HPP

// Pixel descriptors
#include "PixelDescriptors/ImagePatchPixelDescriptor.h"

// Visitors
#include "Visitors/NearestNeighborsDefaultVisitor.hpp"

// Descriptor visitors
#include "Visitors/DescriptorVisitors/ImagePatchDescriptorVisitor.hpp"

// Inpainting visitors
#include "Visitors/InpaintingVisitor.hpp"
#include "Visitors/AcceptanceVisitors/DefaultAcceptanceVisitor.hpp"

// Nearest neighbors functions
#include "NearestNeighbor/LinearSearchBest/IntroducedEnergy.hpp"
#include "NearestNeighbor/LinearSearchBest/FirstAndWrite.hpp"
#include "NearestNeighbor/LinearSearchKNNProperty.hpp"
#include "NearestNeighbor/TwoStepNearestNeighbor.hpp"

// Initializers
#include "Initializers/InitializeFromMaskImage.hpp"
#include "Initializers/InitializePriority.hpp"

// Inpainters
#include "Inpainters/PatchInpainter.hpp"
#include "Inpainters/CompositePatchInpainter.hpp"

// Difference functions
#include "DifferenceFunctions/ImagePatchDifference.hpp"
#include "DifferenceFunctions/SumAbsolutePixelDifference.hpp"
#include "DifferenceFunctions/SumSquaredPixelDifference.hpp"

// Utilities
#include "Utilities/PatchHelpers.h"
#include "Utilities/IndirectPriorityQueue.h"

// Inpainting
#include "Algorithms/InpaintingAlgorithm.hpp"

// Priority
#include "Priority/PriorityRandom.h"
#include "Priority/PriorityCriminisi.h"

// Submodules
#include <Helpers/Helpers.h>
#include <ITKVTKHelpers/ITKVTKHelpers.h>
#include <Mask/MaskOperations.h>

// Boost
#include <boost/graph/grid_graph.hpp>
#include <boost/property_map/property_map.hpp>

// Run with: Data/trashcan.png Data/trashcan.mask 15 filled.png
template <typename TImage>
void InpaintingIntroducedEnergy(TImage* const originalImage, Mask* const mask,
                         const unsigned int patchHalfWidth, const unsigned int numberOfKNN)
{
  itk::ImageRegion<2> fullRegion = originalImage->GetLargestPossibleRegion();

  // Convert the image to HSV
  typedef itk::Image<itk::CovariantVector<float, 3>, 2> HSVImageType;
  HSVImageType::Pointer hsvImage = HSVImageType::New();
  ITKVTKHelpers::ConvertRGBtoHSV(originalImage, hsvImage.GetPointer());

  ITKHelpers::WriteImage(hsvImage.GetPointer(), "HSVImage.mha");

  // Blur the image
  typedef TImage BlurredImageType; // Usually the blurred image is the same type as the original image.
  typename BlurredImageType::Pointer blurredImage = BlurredImageType::New();
  float blurVariance = 2.0f;
  MaskOperations::MaskedBlur(originalImage, mask, blurVariance, blurredImage.GetPointer());

  ITKHelpers::WriteRGBImage(blurredImage.GetPointer(), "BlurredImage.png");

  // Blur the image slightly so that the SSD comparisons are not so noisy
  typename BlurredImageType::Pointer slightBlurredImage = BlurredImageType::New();
  float slightBlurVariance = 1.0f;
  MaskOperations::MaskedBlur(originalImage, mask, slightBlurVariance, slightBlurredImage.GetPointer());

  ITKHelpers::WriteRGBImage(slightBlurredImage.GetPointer(), "SlightlyBlurredImage.png");

  // Create the graph
  typedef ImagePatchPixelDescriptor<TImage> ImagePatchPixelDescriptorType;

  typedef boost::grid_graph<2> VertexListGraphType;

  // We can't make this a signed type (size_t versus int) because we allow negative
  boost::array<std::size_t, 2> graphSideLengths = { { fullRegion.GetSize()[0],
                                                      fullRegion.GetSize()[1] } };
  VertexListGraphType graph(graphSideLengths);
  typedef boost::graph_traits<VertexListGraphType>::vertex_descriptor VertexDescriptorType;
  typedef boost::graph_traits<VertexListGraphType>::vertex_iterator VertexIteratorType;

  // Get the index map
  typedef boost::property_map<VertexListGraphType, boost::vertex_index_t>::const_type IndexMapType;
  IndexMapType indexMap(get(boost::vertex_index, graph));

  // Create the descriptor map. This is where the data for each pixel is stored.
  typedef boost::vector_property_map<ImagePatchPixelDescriptorType, IndexMapType> ImagePatchDescriptorMapType;
  ImagePatchDescriptorMapType imagePatchDescriptorMap(num_vertices(graph), indexMap);

  // Create the patch inpainter.
  typedef PatchInpainter<TImage> OriginalImageInpainterType;
  OriginalImageInpainterType originalImagePatchInpainter(patchHalfWidth, originalImage, mask);
  originalImagePatchInpainter.SetDebugImages(true);
  originalImagePatchInpainter.SetImageName("RGB");
  originalImagePatchInpainter.SetDebugLevel(2); // To also output TargetPatchAfter* files

  // Create an inpainter for the HSV image.
  typedef PatchInpainter<HSVImageType> HSVImageInpainterType;
  HSVImageInpainterType hsvImagePatchInpainter(patchHalfWidth, hsvImage, mask);

  // Create an inpainter for the blurred image.
  typedef PatchInpainter<BlurredImageType> BlurredImageInpainterType;
  BlurredImageInpainterType blurredImagePatchInpainter(patchHalfWidth, blurredImage, mask);

  // Create an inpainter for the slightly blurred image.
  typedef PatchInpainter<BlurredImageType> BlurredImageInpainterType;
  BlurredImageInpainterType slightlyBlurredImagePatchInpainter(patchHalfWidth, slightBlurredImage, mask);

  // Create a composite inpainter.
  CompositePatchInpainter inpainter;
  inpainter.AddInpainter(&originalImagePatchInpainter);
  inpainter.AddInpainter(&hsvImagePatchInpainter);
  inpainter.AddInpainter(&blurredImagePatchInpainter);
  inpainter.AddInpainter(&slightlyBlurredImagePatchInpainter);

  // Create the priority function
  typedef PriorityCriminisi<BlurredImageType> PriorityType;
  PriorityType priorityFunction(blurredImage, mask, patchHalfWidth);
//  priorityFunction.SetDebugLevel(1);

  // Queue
  typedef IndirectPriorityQueue<VertexListGraphType> BoundaryNodeQueueType;
  BoundaryNodeQueueType boundaryNodeQueue(graph);

  // Create the descriptor visitor (use the slightBlurredImage for SSD comparisons).
  typedef ImagePatchDescriptorVisitor<VertexListGraphType, TImage, ImagePatchDescriptorMapType>
      ImagePatchDescriptorVisitorType;
//  ImagePatchDescriptorVisitorType imagePatchDescriptorVisitor(originalImage, mask,
//                                  imagePatchDescriptorMap, patchHalfWidth); // Use the non-blurred image for the SSD comparisons
  ImagePatchDescriptorVisitorType imagePatchDescriptorVisitor(slightBlurredImage, mask,
                                  imagePatchDescriptorMap, patchHalfWidth); // Use the blurred image for the SSD comparisons

  typedef DefaultAcceptanceVisitor<VertexListGraphType> AcceptanceVisitorType;
  AcceptanceVisitorType acceptanceVisitor;

  // Create the inpainting visitor. (The mask is inpainted in FinishVertex)
  typedef InpaintingVisitor<VertexListGraphType, BoundaryNodeQueueType,
      ImagePatchDescriptorVisitorType, AcceptanceVisitorType, PriorityType,
      TImage>
      InpaintingVisitorType;
  InpaintingVisitorType inpaintingVisitor(mask, boundaryNodeQueue,
                                          imagePatchDescriptorVisitor, acceptanceVisitor,
                                          &priorityFunction, patchHalfWidth,
                                          "InpaintingVisitor", originalImage);
  inpaintingVisitor.SetDebugImages(true);
  inpaintingVisitor.SetAllowNewPatches(false);

  InitializePriority(mask, boundaryNodeQueue, &priorityFunction);

  // Initialize the boundary node queue from the user provided mask image.
  InitializeFromMaskImage<InpaintingVisitorType, VertexDescriptorType>(mask, &inpaintingVisitor);
  std::cout << "PatchBasedInpaintingNonInteractive: There are " << boundaryNodeQueue.CountValidNodes()
            << " nodes in the boundaryNodeQueue" << std::endl;

  // Select squared or absolute pixel error
//  typedef ImagePatchDifference<ImagePatchPixelDescriptorType, SumAbsolutePixelDifference<TImage::PixelType> > PatchDifferenceType;
  typedef ImagePatchDifference<ImagePatchPixelDescriptorType,
      SumSquaredPixelDifference<typename TImage::PixelType> > PatchDifferenceType;

  // Create the first (KNN) neighbor finder
  typedef LinearSearchKNNProperty<ImagePatchDescriptorMapType, PatchDifferenceType> KNNSearchType;
  KNNSearchType linearSearchKNN(imagePatchDescriptorMap, numberOfKNN);

  // Setup the second (1-NN) neighbor finder
  typedef std::vector<VertexDescriptorType>::iterator VertexDescriptorVectorIteratorType;

  // Introduced Energy
  typedef LinearSearchBestIntroducedEnergy<ImagePatchDescriptorMapType, TImage> BestSearchType;
  BestSearchType linearSearchBest(imagePatchDescriptorMap, originalImage,
                                  mask, originalImage);
  linearSearchBest.SetDebugImages(true);

  // Setup the two step neighbor finder

  // Without writing top KNN patches
//  TwoStepNearestNeighbor<KNNSearchType, BestSearchType>
//      twoStepNearestNeighbor(linearSearchKNN, linearSearchBest);

  // With writing top KNN patches
  typedef LinearSearchBestFirstAndWrite<ImagePatchDescriptorMapType,
      TImage, PatchDifferenceType> TopPatchesWriterType;
  TopPatchesWriterType topPatchesWriter(imagePatchDescriptorMap, originalImage, mask);

  TwoStepNearestNeighbor<KNNSearchType, BestSearchType, TopPatchesWriterType>
      twoStepNearestNeighbor(linearSearchKNN, linearSearchBest, topPatchesWriter);

  // Perform the inpainting
  InpaintingAlgorithm(graph, inpaintingVisitor, &boundaryNodeQueue,
                      twoStepNearestNeighbor, &inpainter);

}

#endif
