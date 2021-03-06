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

#include "InteractorStyleImageWithDrag.h"

#include <vtkObjectFactory.h>
#include <vtkRenderWindowInteractor.h>

vtkStandardNewMacro(InteractorStyleImageWithDrag);

InteractorStyleImageWithDrag::InteractorStyleImageWithDrag()
{
  this->ImageStyle = vtkSmartPointer<CustomImageStyle>::New();

  this->TrackballStyle = vtkSmartPointer<CustomTrackballStyle>::New();

  this->TrackballStyle->AddObserver(CustomTrackballStyle::PatchesMovedEvent,
                        this, &InteractorStyleImageWithDrag::PatchMoved);
}

void InteractorStyleImageWithDrag::PatchMoved()
{
  this->InvokeEvent(CustomTrackballStyle::PatchesMovedEvent, NULL);
}

void InteractorStyleImageWithDrag::Init()
{
  this->ImageStyle->SetOtherStyle(this->TrackballStyle);
  this->TrackballStyle->SetOtherStyle(this->ImageStyle);

  this->Interactor->SetInteractorStyle(this->ImageStyle);
}

void InteractorStyleImageWithDrag::SetCurrentRenderer(vtkRenderer* const renderer)
{
  this->ImageStyle->SetCurrentRenderer(renderer);
  this->TrackballStyle->SetCurrentRenderer(renderer);
  vtkInteractorStyleTrackballActor::SetCurrentRenderer(renderer);
}

void InteractorStyleImageWithDrag::SetImageOrientation(const double* const leftToRight,
                                                       const double* const bottomToTop)
{
  this->ImageStyle->SetImageOrientation(leftToRight, bottomToTop);
}


CustomImageStyle* InteractorStyleImageWithDrag::GetImageStyle()
{
  return this->ImageStyle;
}

CustomTrackballStyle* InteractorStyleImageWithDrag::GetTrackballStyle()
{
  return this->TrackballStyle;
}

////////////////////////////////
///// CustomTrackballStyle /////
////////////////////////////////
vtkStandardNewMacro(CustomTrackballStyle);

void CustomTrackballStyle::OnLeftButtonDown()
{
  //std::cout << "CustomTrackballStyle::OnLeftButtonDown()" << std::endl;
  // Behave like the middle button instead
  vtkInteractorStyleTrackballActor::OnMiddleButtonDown();
  
}

void CustomTrackballStyle::OnLeftButtonUp()
{
  //std::cout << "TrackballStyle::OnLeftButtonUp()" << std::endl;
  // Behave like the middle button instead
  vtkInteractorStyleTrackballActor::OnMiddleButtonUp();
  this->InvokeEvent(this->PatchesMovedEvent, NULL);
}

void CustomTrackballStyle::OnMiddleButtonDown()
{
  this->Interactor->SetInteractorStyle(this->OtherStyle);
  this->OtherStyle->OnMiddleButtonDown();
}

void CustomTrackballStyle::OnRightButtonDown()
{
  this->Interactor->SetInteractorStyle(this->OtherStyle);
  this->OtherStyle->OnRightButtonDown();
}

void CustomTrackballStyle::SetOtherStyle(CustomImageStyle* style)
{
  this->OtherStyle = style;
}

////////////////////////////////
///// CustomImageStyle /////////
////////////////////////////////

vtkStandardNewMacro(CustomImageStyle);

void CustomImageStyle::OnLeftButtonDown()
{
  //std::cout << "CustomImageStyle::OnLeftButtonDown()" << std::endl;
  // Behave like the middle button instead
  this->Interactor->SetInteractorStyle(this->OtherStyle);
  this->OtherStyle->OnLeftButtonDown();
}

void CustomImageStyle::SetOtherStyle(CustomTrackballStyle* const style)
{
  this->OtherStyle = style;
}
