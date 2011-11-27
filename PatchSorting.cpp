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

#include "PatchSorting.h"

//////////////////////////////////////////
/////// Sorting functions //////////////
//////////////////////////////////////////
bool SortByDepthDifference(const PatchPair& pair1, const PatchPair& pair2)
{
  return (pair1.GetDepthDifference() < pair2.GetDepthDifference());
}

bool SortByColorDifference(const PatchPair& pair1, const PatchPair& pair2)
{
  return (pair1.GetColorDifference() < pair2.GetColorDifference());
}

bool SortByAverageSquaredDifference(const PatchPair& pair1, const PatchPair& pair2)
{
  return (pair1.GetAverageSquaredDifference() < pair2.GetAverageSquaredDifference());
}

bool SortByAverageAbsoluteDifference(const PatchPair& pair1, const PatchPair& pair2)
{
  return (pair1.GetAverageAbsoluteDifference() < pair2.GetAverageAbsoluteDifference());
}

bool SortByBoundaryGradientDifference(const PatchPair& pair1, const PatchPair& pair2)
{
  return (pair1.GetBoundaryGradientDifference() < pair2.GetBoundaryGradientDifference());
}

bool SortByBoundaryPixelDifference(const PatchPair& pair1, const PatchPair& pair2)
{
  return (pair1.GetBoundaryPixelDifference() < pair2.GetBoundaryPixelDifference());
}

bool SortByBoundaryIsophoteAngleDifference(const PatchPair& pair1, const PatchPair& pair2)
{
  return (pair1.GetBoundaryIsophoteAngleDifference() < pair2.GetBoundaryIsophoteAngleDifference());
}

bool SortByBoundaryIsophoteStrengthDifference(const PatchPair& pair1, const PatchPair& pair2)
{
  return (pair1.GetBoundaryIsophoteStrengthDifference() < pair2.GetBoundaryIsophoteStrengthDifference());
}

bool SortByTotalScore(const PatchPair& pair1, const PatchPair& pair2)
{
  return (pair1.GetTotalScore() < pair2.GetTotalScore());
}

bool SortByDepthAndColor::operator()(const PatchPair& pair1, const PatchPair& pair2)
{
  return (pair1.GetDepthAndColorDifference() < pair2.GetDepthAndColorDifference());
}
