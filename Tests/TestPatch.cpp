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

#include "Patch.h"

int main(int argc, char*argv[])
{
  // Create a patch
  itk::Index<2> corner0;
  corner0.Fill(0);

  itk::Size<2> patchSize;
  patchSize.Fill(10);

  itk::ImageRegion<2> region0(corner0, patchSize);
  Patch patch0(region0);

  if(patch0.GetRegion() != region0)
    {
    std::cerr << "Region was not set or retrieved correctly!" << std::endl;
    return EXIT_FAILURE;
    }

  if(patch0 == patch0)
    {
    // Good, this is what we want.
    }
  else
    {
    std::cerr << "patch0 should == patch0 but does not!" << std::endl;
    return EXIT_FAILURE;
    }

  if(patch0 != patch0)
    {
    std::cerr << "patch0 != patch0 but it should!" << std::endl;
    return EXIT_FAILURE;
    }

  // Create another patch
  itk::Index<2> corner1;
  corner1.Fill(5);

  itk::ImageRegion<2> region1(corner1, patchSize);
  Patch patch1(region1);

  if(patch0 == patch1)
    {
    std::cerr << "patch0 == patch1 but should not!" << std::endl;
    return EXIT_FAILURE;
    }

  if(patch0 != patch1)
    {
    // Good, this is what we want.
    }
  else
    {
    std::cerr << "patch0 != patch1 failed - they should not be equal!" << std::endl;
    return EXIT_FAILURE;
    }

  if(patch0 < patch1)
    {
    // Good, this is what we want.
    }
  else
    {
    std::cerr << "patch0 < patch1 failed!" << std::endl;
    return EXIT_FAILURE;
    }

  return EXIT_SUCCESS;
}