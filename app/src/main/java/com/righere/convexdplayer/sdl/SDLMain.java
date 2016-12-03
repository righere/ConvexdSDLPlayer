package com.righere.convexdplayer.sdl;

import android.util.Log;

/**
 Simple nativeInit() runnable
 */
public class SDLMain implements Runnable {
    @Override
    public void run() {

        String videoPath = SDLActivity.mSingleton.getPath();
        // Runs SDL_main()
        SDLActivity.nativeInit(videoPath);

        Log.v("SDL", "SDL thread terminated");
    }
}
