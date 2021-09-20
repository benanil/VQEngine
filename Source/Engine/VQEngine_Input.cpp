//	VQE
//	Copyright(C) 2020  - Volkan Ilbeyli
//
//	This program is free software : you can redistribute it and / or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation, either version 3 of the License, or
//	(at your option) any later version.
//
//	This program is distributed in the hope that it will be useful,
//	but WITHOUT ANY WARRANTY; without even the implied warranty of
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
//	GNU General Public License for more details.
//
//	You should have received a copy of the GNU General Public License
//	along with this program.If not, see <http://www.gnu.org/licenses/>.
//
//	Contact: volkanilbeyli@gmail.com

#define NOMINMAX

#include "VQEngine.h"
#include "Math.h"
#include "Scene/Scene.h"
#include "../Scenes/Scenes.h" // scene instances

#include "GPUMarker.h"

#include "Libs/VQUtils/Source/utils.h"

#include <algorithm>

#include <dwmapi.h>
#pragma comment(lib, "Dwmapi.lib")

#if !VQENGINE_MT_PIPELINED_UPDATE_AND_RENDER_THREADS
constexpr int FRAME_DATA_INDEX = 0;
#endif
static void Toggle(bool& b) { b = !b; }




void VQEngine::HandleEngineInput()
{
#if VQENGINE_MT_PIPELINED_UPDATE_AND_RENDER_THREADS
	const int NUM_BACK_BUFFERS = mRenderer.GetSwapChainBackBufferCount(mpWinMain->GetHWND());
	const int FRAME_DATA_INDEX = mNumUpdateLoopsExecuted % NUM_BACK_BUFFERS;
#endif

	for (decltype(mInputStates)::iterator it = mInputStates.begin(); it != mInputStates.end(); ++it)
	{
		HWND   hwnd = it->first;
		Input& input = it->second;
		auto& pWin = this->GetWindow(hwnd);
		const bool bIsShiftDown = input.IsKeyDown("Shift");

		//
		// Process-level input handling
		//
		if (input.IsKeyTriggered("Esc"))
		{
			if (pWin->IsMouseCaptured())
			{
				mEventQueue_VQEToWin_Main.AddItem(std::make_shared<SetMouseCaptureEvent>(hwnd, false, true));
			}
		}
		if (input.IsAnyMouseDown())
		{
			Input& inp = mInputStates.at(hwnd);
			if (inp.GetInputBypassing())
			{
				inp.SetInputBypassing(false);

				// capture mouse only when main window is clicked
				if (hwnd == mpWinMain->GetHWND())
					mEventQueue_VQEToWin_Main.AddItem(std::make_shared<SetMouseCaptureEvent>(hwnd, true, false));
			}
		}
	}
}

void VQEngine::HandleUIInput()
{
	for (decltype(mInputStates)::iterator it = mInputStates.begin(); it != mInputStates.end(); ++it)
	{
		HWND   hwnd = it->first;
		Input& input = it->second;
		auto& pWin = this->GetWindow(hwnd);
		const bool bIsShiftDown = input.IsKeyDown("Shift");

		if (pWin == mpWinMain)
		{
			if (input.IsKeyTriggered("F1")) Toggle(mUIState.bWindowVisible_SceneControls);
			if (input.IsKeyTriggered("F2")) Toggle(mUIState.bWindowVisible_Profiler);
			if (input.IsKeyTriggered("F3")) Toggle(mUIState.bWindowVisible_PostProcessControls);
			if (input.IsKeyTriggered("F4")) Toggle(mUIState.bWindowVisible_DebugPanel);
			if (input.IsKeyTriggered("F5")) Toggle(mUIState.bWindowVisible_GraphicsSettingsPanel);

			if (input.IsKeyTriggered("B"))
			{
				WaitUntilRenderingFinishes();
				FPostProcessParameters& PPParams = mpScene->GetPostProcessParameters(FRAME_DATA_INDEX);
				PPParams.bEnableCAS = !PPParams.bEnableCAS;
				Log::Info("Toggle FFX-CAS: %d", PPParams.bEnableCAS);
			}
		}
	}
}


void VQEngine::HandleMainWindowInput(Input& input, HWND hwnd)
{
#if VQENGINE_MT_PIPELINED_UPDATE_AND_RENDER_THREADS
	const int NUM_BACK_BUFFERS = mRenderer.GetSwapChainBackBufferCount(mpWinMain->GetHWND());
	const int FRAME_DATA_INDEX = mNumUpdateLoopsExecuted % NUM_BACK_BUFFERS;
#endif
	const bool bIsShiftDown = input.IsKeyDown("Shift");
	//const bool bIsAltDown = input.IsKeyDown("Alt"); // Alt+Z detection doesn't work, TODO: fix
	const bool bIsAltDown = (GetKeyState(VK_MENU) & 0x8000) != 0; // Alt+Z detection doesn't work, TODO: fix
	const bool bMouseLeftTriggered = input.IsMouseTriggered(Input::EMouseButtons::MOUSE_BUTTON_LEFT);
	const bool bMouseRightTriggered = input.IsMouseTriggered(Input::EMouseButtons::MOUSE_BUTTON_RIGHT);
	const bool bMouseLeftReleased = input.IsMouseReleased(Input::EMouseButtons::MOUSE_BUTTON_LEFT);
	const bool bMouseRightReleased = input.IsMouseReleased(Input::EMouseButtons::MOUSE_BUTTON_RIGHT);

	// Mouse Capture & Visibility
	if (bMouseLeftTriggered || bMouseRightTriggered)
	{
		const bool bCapture = true;
		const bool bVisible = false;
		mEventQueue_VQEToWin_Main.AddItem(std::make_shared< SetMouseCaptureEvent>(hwnd, bCapture, bVisible));
	}
	if (bMouseLeftReleased || bMouseRightReleased)
	{
		const bool bCapture = false;
		const bool bVisible = true;
		mEventQueue_VQEToWin_Main.AddItem(std::make_shared< SetMouseCaptureEvent>(hwnd, bCapture, bVisible));
	}

	// UI
	auto Toggle = [](bool& b) {b = !b; };
	if ((bIsAltDown && input.IsKeyTriggered("Z")) // Alt+Z detection doesn't work, TODO: fix
		|| (bIsShiftDown && input.IsKeyTriggered("Z"))) // woraround: use shift+z for now
	{
		Toggle(mUIState.bHideAllWindows);
	}

	// Graphics Settings Controls
	if (input.IsKeyTriggered("V"))
	{
		auto& SwapChain = mRenderer.GetWindowSwapChain(hwnd);
		mEventQueue_WinToVQE_Renderer.AddItem(std::make_shared<SetVSyncEvent>(hwnd, !SwapChain.IsVSyncOn()));
	}
	if (input.IsKeyTriggered("M"))
	{
		mSettings.gfx.bAntiAliasing = !mSettings.gfx.bAntiAliasing;
		Log::Info("Toggle MSAA: %d", mSettings.gfx.bAntiAliasing);
	}

	if (input.IsKeyTriggered("G"))
	{
		FPostProcessParameters& PPParams = mpScene->GetPostProcessParameters(FRAME_DATA_INDEX);
		PPParams.TonemapperParams.ToggleGammaCorrection = PPParams.TonemapperParams.ToggleGammaCorrection == 1 ? 0 : 1;
		Log::Info("Tonemapper: ApplyGamma=%d (SDR-only)", PPParams.TonemapperParams.ToggleGammaCorrection);
	}

	// Scene switching
	if (!mbLoadingLevel)
	{
		if (bIsShiftDown)
		{
			const int NumScenes = static_cast<int>(mResourceNames.mSceneNames.size());
			if (input.IsKeyTriggered("PageUp")) { mIndex_SelectedScene = CircularIncrement(mIndex_SelectedScene, NumScenes);     this->StartLoadingScene(mIndex_SelectedScene); }
			if (input.IsKeyTriggered("PageDown")) { mIndex_SelectedScene = CircularDecrement(mIndex_SelectedScene, NumScenes - 1); this->StartLoadingScene(mIndex_SelectedScene); }
			if (input.IsKeyTriggered("R")) { this->StartLoadingScene(mIndex_SelectedScene); } // reload scene
		}
		if (input.IsKeyTriggered("1")) { mIndex_SelectedScene = 0; this->StartLoadingScene(mIndex_SelectedScene); }
		if (input.IsKeyTriggered("2")) { mIndex_SelectedScene = 1; this->StartLoadingScene(mIndex_SelectedScene); }
		if (input.IsKeyTriggered("3")) { mIndex_SelectedScene = 2; this->StartLoadingScene(mIndex_SelectedScene); }
		if (input.IsKeyTriggered("4")) { mIndex_SelectedScene = 3; this->StartLoadingScene(mIndex_SelectedScene); }
	}
}