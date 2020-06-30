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

#include "VQEngine.h"

#include <d3d12.h>
#include <dxgi.h>


void VQEngine::RenderThread_Main()
{
	Log::Info("RenderThread_Main()");
	this->RenderThread_Inititalize();

	this->mRenderer.Load();

	bool bQuit = false;
	while (!this->mbStopAllThreads && !bQuit)
	{
		RenderThread_HandleEvents();

		RenderThread_WaitForUpdateThread();

#if DEBUG_LOG_THREAD_SYNC_VERBOSE
		Log::Info(/*"RenderThread_Tick() : */"r%d (u=%llu)", mNumRenderLoopsExecuted.load(), mNumUpdateLoopsExecuted.load());
#endif

		RenderThread_PreRender();
		RenderThread_Render();

		++mNumRenderLoopsExecuted;

		RenderThread_SignalUpdateThread();

		RenderThread_HandleEvents();
	}

	this->mRenderer.Unload();

	this->RenderThread_Exit();
	Log::Info("RenderThread_Main() : Exit");
}


void VQEngine::RenderThread_WaitForUpdateThread()
{
#if DEBUG_LOG_THREAD_SYNC_VERBOSE
	Log::Info("r:wait : u=%llu, r=%llu", mNumUpdateLoopsExecuted.load(), mNumRenderLoopsExecuted.load());
#endif

	mpSemRender->Wait();
}

void VQEngine::RenderThread_SignalUpdateThread()
{
	mpSemUpdate->Signal();
}






void VQEngine::RenderThread_Inititalize()
{
	FRendererInitializeParameters params = {};
	params.Settings = mSettings.gfx;
	params.Windows.push_back(FWindowRepresentation(mpWinMain , mSettings.gfx.bVsync, mSettings.WndMain.DisplayMode == EDisplayMode::EXCLUSIVE_FULLSCREEN));
	if(mpWinDebug) params.Windows.push_back(FWindowRepresentation(mpWinDebug, false, false));

	mRenderer.Initialize(params);

	auto fnHandleWindowTransitions = [&](std::unique_ptr<Window>& pWin, const FWindowSettings& settings)
	{
		if (!pWin) return;

		// TODO: generic solution to multi window/display settings. 
		//       For now, simply prevent debug wnd occupying main wnd's display.
		if ( mpWinMain->IsFullscreen()
			&& (mSettings.WndMain.PreferredDisplay == mSettings.WndDebug.PreferredDisplay)
			&& settings.IsDisplayModeFullscreen()
			&& pWin != mpWinMain)
		{
			Log::Warning("Debug window and Main window cannot be Fullscreen on the same display!");
			pWin->SetFullscreen(false);
			// TODO: as a more graceful fallback, move it to the next monitor and keep fullscreen
			return;
		}

		// Borderless fullscreen transitions are handled through Window object
		// Exclusive  fullscreen transitions are handled through the Swapchain
		if (settings.DisplayMode == EDisplayMode::BORDERLESS_FULLSCREEN)
		{
			// TODO: preferred screen impl
			if(pWin) pWin->ToggleWindowedFullscreen();
		}
	};

	fnHandleWindowTransitions(mpWinMain , mSettings.WndMain);
	fnHandleWindowTransitions(mpWinDebug, mSettings.WndDebug);

	mbRenderThreadInitialized.store(true);
	mNumRenderLoopsExecuted.store(0);
}

void VQEngine::RenderThread_Exit()
{
	mRenderer.Exit();
}

void VQEngine::RenderThread_PreRender()
{
}

void VQEngine::RenderThread_Render()
{
	const int NUM_BACK_BUFFERS = mRenderer.GetSwapChainBackBufferCountOfWindow(mpWinMain);
	const int FRAME_DATA_INDEX  = mNumRenderLoopsExecuted % NUM_BACK_BUFFERS;

	HRESULT hr0 = S_OK;
	HRESULT hr1 = S_OK;

#define RENDER_IN_PARALLEL 0
#if RENDER_IN_PARALLEL
	// TODO: test parallel rendering of window contexts
#else
	if (mbLoadingLevel)
	{
		// TODO: proper data encalsulation + remove reinterpret cast.
		//       this was good enough for testing threaded loading and is meant to be temporary.
		hr0 = mRenderer.RenderWindowContext(mpWinMain->GetHWND() , *reinterpret_cast<FFrameData*>(&mScene_MainWnd.mLoadingScreenData[FRAME_DATA_INDEX]));
		if(mpWinDebug) hr1 = mRenderer.RenderWindowContext(mpWinDebug->GetHWND(), *reinterpret_cast<FFrameData*>(&mScene_DebugWnd.mLoadingScreenData[FRAME_DATA_INDEX]));
	}

	// Scene rendering
	else
	{
		hr0 = mRenderer.RenderWindowContext(mpWinMain->GetHWND(), mScene_MainWnd.mFrameData[FRAME_DATA_INDEX]);
		if(mpWinDebug) hr1 = mRenderer.RenderWindowContext(mpWinDebug->GetHWND(), mScene_DebugWnd.mFrameData[FRAME_DATA_INDEX]);
	}
#endif

	// TODO: remove copy paste and use encapsulation of context rendering properly
	// currently check only for hr0 for the mainWindow
	if (hr0 == DXGI_STATUS_OCCLUDED)
	{
		if (mpWinMain->IsFullscreen()) 
		{
			mpWinMain->SetFullscreen(false);
			mpWinMain->Show();
		}
	}
}




// ------------------------------------------------------------------------------------------------------------------------------------------------------------
//
// EVENT HANDLING
//
// ------------------------------------------------------------------------------------------------------------------------------------------------------------
void VQEngine::RenderThread_HandleEvents()
{
	// Swap event recording buffers so we can read & process a limited number of events safely.
	//   Otherwise, theoretically the producer (Main) thread could keep adding new events 
	//   while we're spinning on the queue items below, and cause render thread to stall while, say, resizing.
	mWinEventQueue.SwapBuffers();
	std::queue<std::unique_ptr<IEvent>>& q = mWinEventQueue.GetBackContainer();
	if (q.empty())
		return;

	// process the events
	std::shared_ptr<IEvent> pEvent = nullptr;
	std::shared_ptr<WindowResizeEvent> pResizeEvent = nullptr;
	while (!q.empty())
	{
		pEvent = std::move(q.front());
		q.pop();

		switch (pEvent->mType)
		{
		case EEventType::WINDOW_RESIZE_EVENT: 
			// noop, we only care about the last RESIZE event to avoid calling SwapchainResize() unneccessarily
			pResizeEvent = std::static_pointer_cast<WindowResizeEvent>(pEvent);

			break;
		case EEventType::TOGGLE_FULLSCREEN_EVENT:
			// handle every fullscreen event
			RenderThread_HandleToggleFullscreenEvent(pEvent.get());
			break;
		}
	}
	// Process Window Resize
	if (pResizeEvent)
	{
		RenderThread_HandleResizeWindowEvent(pResizeEvent.get());
	}
	
}

void VQEngine::RenderThread_HandleResizeWindowEvent(const IEvent* pEvent)
{
	const WindowResizeEvent* pResizeEvent = static_cast<const WindowResizeEvent*>(pEvent);
	const HWND&                      hwnd = pResizeEvent->hwnd;
	const int                       WIDTH = pResizeEvent->width;
	const int                      HEIGHT = pResizeEvent->height;
	SwapChain&                  Swapchain = mRenderer.GetWindowSwapChain(hwnd);
	std::unique_ptr<Window>&         pWnd = GetWindow(hwnd);

	Log::Info("RenderThread: Handle Resize event, set resolution to %dx%d", WIDTH , HEIGHT);

	Swapchain.WaitForGPU();
	Swapchain.Resize(WIDTH, HEIGHT);
	pWnd->OnResize(WIDTH, HEIGHT);
	mRenderer.OnWindowSizeChanged(hwnd, WIDTH, HEIGHT);
}

void VQEngine::RenderThread_HandleToggleFullscreenEvent(const IEvent* pEvent)
{
	const ToggleFullscreenEvent* pToggleFSEvent = static_cast<const ToggleFullscreenEvent*>(pEvent);
	HWND                                   hwnd = pToggleFSEvent->hwnd;
	SwapChain&                        Swapchain = mRenderer.GetWindowSwapChain(pToggleFSEvent->hwnd);
	const FWindowSettings&          WndSettings = GetWindowSettings(hwnd);
	const bool   bExclusiveFullscreenTransition = WndSettings.DisplayMode == EDisplayMode::EXCLUSIVE_FULLSCREEN;
	const bool            bFullscreenStateToSet = !Swapchain.IsFullscreen();
	std::unique_ptr<Window>&               pWnd = GetWindow(hwnd);

	Log::Info("RenderThread: Handle Fullscreen(exclusiveFS=%s) transition to %dx%d"
		, (bFullscreenStateToSet ? "true" : "false")
		, WndSettings.Width
		, WndSettings.Height
	);

	// if we're transitioning into Fullscreen, save the current window dimensions
	if (bFullscreenStateToSet)
	{
		FWindowSettings& WndSettings_ = GetWindowSettings(hwnd);
		WndSettings_.Width  = pWnd->GetWidth();
		WndSettings_.Height = pWnd->GetHeight();
	}

	Swapchain.WaitForGPU(); // make sure GPU is finished


	//
	// EXCLUSIVE FULLSCREEN
	//
	if (bExclusiveFullscreenTransition)
	{
		// Swapchain handles resizing the window through SetFullscreenState() call
		Swapchain.SetFullscreen(bFullscreenStateToSet, WndSettings.Width, WndSettings.Height);

		// TODO: capture/release mouse
		
		if (!bFullscreenStateToSet)
		{
			// If the Swapchain is created in fullscreen mode, the WM_PAINT message will not be 
			// received upon switching to windowed mode (ALT+TAB or ALT+ENTER) and the window
			// will be visible, but not interactable and also not visible in taskbar.
			// Explicitly calling Show() here fixes the situation.
			pWnd->Show();
		}
	}

	//
	// BORDERLESS FULLSCREEN
	//
	else 
	{
		GetWindow(hwnd)->ToggleWindowedFullscreen(&Swapchain);
	}
}

