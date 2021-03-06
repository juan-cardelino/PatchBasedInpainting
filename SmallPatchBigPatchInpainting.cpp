/*=========================================================================
 *
 *  Copyright David Doria 2011 daviddoria@gmail.com
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

// Custom
#include "Helpers/HelpersOutput.h"

// Pixel descriptors
#include "PixelDescriptors/ImagePatchPixelDescriptor.h"

// Descriptor visitors
#include "Visitors/DescriptorVisitors/ImagePatchDescriptorVisitor.hpp"
#include "Visitors/DescriptorVisitors/CompositeDescriptorVisitor.hpp"

// Inpainting visitors
#include "Visitors/InpaintingVisitor.hpp"

// Nearest neighbors
#include "NearestNeighbor/LinearSearchBestProperty.hpp"
#include "NearestNeighbor/LinearSearchKNNProperty.hpp"
#include "NearestNeighbor/TwoStepNearestNeighbor.hpp"

// Topologies
#include "Topologies/ImagePatchTopology.hpp"

// Initializers
#include "Initializers/InitializeFromMaskImage.hpp"
#include "Initializers/InitializePriority.hpp"

// Inpainters
#include "Inpainters/MaskedGridPatchInpainter.hpp"
#include "Inpainters/HoleListPatchInpainter.hpp"

// Difference functions
#include "DifferenceFunctions/ImagePatchDifference.hpp"
#include "DifferenceFunctions/FeatureVectorDifference.hpp"

// Inpainting
#include "Algorithms/InpaintingAlgorithm.hpp"

// Priority
#include "Priority/PriorityRandom.h"

// ITK
#include "itkImageFileReader.h"

// Boost
#include <boost/graph/grid_graph.hpp>
#include <boost/property_map/property_map.hpp>
#include <boost/graph/detail/d_ary_heap.hpp>

// Debug
#include "Helpers/HelpersOutput.h"

// Run with: Data/trashcan.mha Data/trashcan_mask.mha 5  10 filled.mha
int main(int argc, char *argv[])
{
  // Verify arguments
  if(argc != 6)
    {
    std::cerr << "Required arguments: image.mha imageMask.mha small_patch_half_width big_patch_half_width output.mha" << std::endl;
    std::cerr << "Input arguments: ";
    for(int i = 1; i < argc; ++i)
      {
      std::cerr << argv[i] << " ";
      }
    return EXIT_FAILURE;
    }

  // Parse arguments
  std::string imageFilename = argv[1];
  std::string maskFilename = argv[2];

  std::stringstream ssSmallPatchHalfWidth;
  ssSmallPatchHalfWidth << argv[3];
  unsigned int small_patch_half_width = 0;
  ssSmallPatchHalfWidth >> small_patch_half_width;
  
  std::stringstream ssBigPatchHalfWidth;
  ssBigPatchHalfWidth << argv[4];
  unsigned int big_patch_half_width = 0;
  ssBigPatchHalfWidth >> big_patch_half_width;

  std::string outputFilename = argv[5];

  // Output arguments
  std::cout << "Reading image: " << imageFilename << std::endl;
  std::cout << "Reading mask: " << maskFilename << std::endl;
  std::cout << "Small Patch half width: " << small_patch_half_width << std::endl;
  std::cout << "Big Patch half width: " << big_patch_half_width << std::endl;
  std::cout << "Output: " << outputFilename << std::endl;

  typedef FloatVectorImageType ImageType;

  typedef  itk::ImageFileReader<ImageType> ImageReaderType;
  ImageReaderType::Pointer imageReader = ImageReaderType::New();
  imageReader->SetFileName(imageFilename);
  imageReader->Update();

  ImageType::Pointer image = ImageType::New();
  ITKHelpers::DeepCopy(imageReader->GetOutput(), image.GetPointer());

  Mask::Pointer mask = Mask::New();
  mask->Read(maskFilename);

  std::cout << "number of hole pixels: " << mask->CountHolePixels() << std::endl;
  std::cout << "number of valid pixels: " << mask->CountValidPixels() << std::endl;

  typedef ImagePatchPixelDescriptor<ImageType> ImagePatchPixelDescriptorType;

  // Create the graph
  typedef boost::grid_graph<2> VertexListGraphType;
  boost::array<std::size_t, 2> graphSideLengths = { { imageReader->GetOutput()->GetLargestPossibleRegion().GetSize()[0],
                                                      imageReader->GetOutput()->GetLargestPossibleRegion().GetSize()[1] } };
  VertexListGraphType graph(graphSideLengths);
  typedef boost::graph_traits<VertexListGraphType>::vertex_descriptor VertexDescriptorType;

  // Get the index map
  typedef boost::property_map<VertexListGraphType, boost::vertex_index_t>::const_type IndexMapType;
  IndexMapType indexMap(get(boost::vertex_index, graph));

  // Create the priority map
  typedef boost::vector_property_map<float, IndexMapType> PriorityMapType;
  PriorityMapType priorityMap(num_vertices(graph), indexMap);

  // Create the node fill status map. Each pixel is either filled (true) or not filled (false).
  typedef boost::vector_property_map<bool, IndexMapType> FillStatusMapType;
  FillStatusMapType fillStatusMap(num_vertices(graph), indexMap);

  // Create the boundary status map. A node is on the current boundary if this property is true. 
  // This property helps the boundaryNodeQueue because we can mark here if a node has become no longer
  // part of the boundary, so when the queue is popped we can check this property to see if it should
  // actually be processed.
  typedef boost::vector_property_map<bool, IndexMapType> BoundaryStatusMapType;
  BoundaryStatusMapType boundaryStatusMap(num_vertices(graph), indexMap);

  // Create the small and big patch descriptor maps.
  typedef boost::vector_property_map<ImagePatchPixelDescriptorType, IndexMapType> ImagePatchDescriptorMapType;
  ImagePatchDescriptorMapType smallImagePatchDescriptorMap(num_vertices(graph), indexMap);
  ImagePatchDescriptorMapType bigImagePatchDescriptorMap(num_vertices(graph), indexMap);

  // Create the patch inpainter. The inpainter needs to know the status of each pixel to determine if they should be inpainted.
  typedef MaskedGridPatchInpainter<FillStatusMapType> InpainterType;
  InpainterType patchInpainter(big_patch_half_width, fillStatusMap);

  // Create the priority function
  typedef PriorityRandom PriorityType;
  PriorityType priorityFunction;

  // Create the boundary node queue. The priority of each node is used to order the queue.
  typedef boost::vector_property_map<std::size_t, IndexMapType> IndexInHeapMap;
  IndexInHeapMap index_in_heap(indexMap);

  // Create the priority compare functor
  typedef std::less<float> PriorityCompareType;
  PriorityCompareType lessThanFunctor;

  typedef boost::d_ary_heap_indirect<VertexDescriptorType, 4, IndexInHeapMap, PriorityMapType, PriorityCompareType> BoundaryNodeQueueType;
  BoundaryNodeQueueType boundaryNodeQueue(priorityMap, index_in_heap, lessThanFunctor);

  // Create the descriptor visitors
  typedef ImagePatchDescriptorVisitor<VertexListGraphType, ImageType, ImagePatchDescriptorMapType> ImagePatchDescriptorVisitorType;
  ImagePatchDescriptorVisitorType smallImagePatchDescriptorVisitor(image, mask, smallImagePatchDescriptorMap, small_patch_half_width);
  
  typedef ImagePatchDescriptorVisitor<VertexListGraphType, ImageType, ImagePatchDescriptorMapType> ImagePatchDescriptorVisitorType;
  ImagePatchDescriptorVisitorType bigImagePatchDescriptorVisitor(image, mask, bigImagePatchDescriptorMap, big_patch_half_width);
  
  typedef CompositeDescriptorVisitor<VertexListGraphType> CompositeDescriptorVisitorType;
  CompositeDescriptorVisitorType compositeDescriptorVisitor;
  compositeDescriptorVisitor.AddVisitor(&smallImagePatchDescriptorVisitor);
  compositeDescriptorVisitor.AddVisitor(&bigImagePatchDescriptorVisitor);
  
  // Create the inpainting visitor. The big patch size is used to actually inpaint.
  typedef InpaintingVisitor<VertexListGraphType, ImageType, BoundaryNodeQueueType, FillStatusMapType,
                            CompositeDescriptorVisitorType, PriorityType, PriorityMapType, BoundaryStatusMapType> InpaintingVisitorType;
  InpaintingVisitorType inpaintingVisitor(image, mask, boundaryNodeQueue, fillStatusMap,
                                          compositeDescriptorVisitor, priorityMap, &priorityFunction, big_patch_half_width, boundaryStatusMap);

  InitializePriority(mask, boundaryNodeQueue, priorityMap, &priorityFunction, boundaryStatusMap);

  // Initialize the boundary node queue from the user provided mask image.
  InitializeFromMaskImage(mask, &inpaintingVisitor, graph, fillStatusMap);
  std::cout << "PatchBasedInpaintingNonInteractive: There are " << boundaryNodeQueue.size()
            << " nodes in the boundaryNodeQueue" << std::endl;

  // Create the nearest neighbor finder
  typedef LinearSearchKNNProperty<ImagePatchDescriptorMapType, ImagePatchDifference<ImagePatchPixelDescriptorType> > KNNSearchType;
  KNNSearchType linearSearchKNN(smallImagePatchDescriptorMap, 1000);

  typedef LinearSearchBestProperty<ImagePatchDescriptorMapType, ImagePatchDifference<ImagePatchPixelDescriptorType> > BestSearchType;
  BestSearchType linearSearchBest(bigImagePatchDescriptorMap);

  TwoStepNearestNeighbor<KNNSearchType, BestSearchType> twoStepNearestNeighbor(linearSearchKNN, linearSearchBest);

  // Perform the inpainting
  inpainting_loop(graph, inpaintingVisitor, boundaryStatusMap, boundaryNodeQueue, twoStepNearestNeighbor, patchInpainter);

  HelpersOutput::WriteImage<ImageType>(image, outputFilename);

  return EXIT_SUCCESS;
}
