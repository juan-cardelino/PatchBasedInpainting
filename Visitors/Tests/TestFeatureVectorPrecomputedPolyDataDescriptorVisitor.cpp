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

// Pixel descriptors
#include "PixelDescriptors/FeatureVectorPixelDescriptor.h"

// Visitors
#include "Visitors/DescriptorVisitors/FeatureVectorPrecomputedPolyDataDescriptorVisitor.hpp"

// VTK
#include <vtkPolyData.h>
#include <vtkSmartPointer.h>

// Boost
#include <boost/graph/grid_graph.hpp>
#include <boost/property_map/property_map.hpp>
#include <boost/graph/detail/d_ary_heap.hpp>

// Debug
#include "Helpers/HelpersOutput.h"

int main(int argc, char *argv[])
{
  typedef FeatureVectorPixelDescriptor FeatureVectorPixelDescriptorType;

  // Create the graph
  typedef boost::grid_graph<2> VertexListGraphType;
  const unsigned int graphSize = 10;
  boost::array<std::size_t, 2> graphSideLengths = { { graphSize, graphSize } };
  VertexListGraphType graph(graphSideLengths);
  typedef boost::graph_traits<VertexListGraphType>::vertex_descriptor VertexDescriptorType;

  // Get the index map
  typedef boost::property_map<VertexListGraphType, boost::vertex_index_t>::const_type IndexMapType;
  IndexMapType indexMap(get(boost::vertex_index, graph));

  // Create the descriptor map. This is where the data for each pixel is stored.
  typedef boost::vector_property_map<FeatureVectorPixelDescriptorType, IndexMapType> FeatureVectorDescriptorMapType;
  FeatureVectorDescriptorMapType featureVectorDescriptorMap(num_vertices(graph), indexMap);

  vtkSmartPointer<vtkPolyData> polydata = vtkSmartPointer<vtkPolyData>::New();
  std::string featureName = "test";
  typedef FeatureVectorPrecomputedPolyDataDescriptorVisitor<VertexListGraphType, FeatureVectorDescriptorMapType> FeatureVectorPrecomputedPolyDataDescriptorVisitorType;
  FeatureVectorPrecomputedPolyDataDescriptorVisitorType featureVectorPrecomputedPolyDataDescriptorVisitor(featureVectorDescriptorMap, polydata, featureName);
  return 0;
}
