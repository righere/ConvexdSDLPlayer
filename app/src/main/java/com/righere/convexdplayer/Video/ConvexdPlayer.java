package com.righere.convexdplayer.Video;

import android.content.Intent;
import android.os.Bundle;
import android.support.annotation.Nullable;
import android.support.v7.app.AppCompatActivity;
import android.util.Log;

import com.righere.convexdplayer.R;
import com.righere.convexdplayer.sdl.SDLActivity;

/**
 * Created by righere on 16-11-21.
 * ConvexdPlayer main player View
 */

public class ConvexdPlayer extends SDLActivity{

    private static final String TAG = "ConvexdPlayer";

    // Used to load the 'native-lib' library on application startup.
    static {
        System.loadLibrary("SDL2main");
//        System.loadLibrary("SDL");
    }


    @Override
    public void onCreate(@Nullable Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_convexdplayer);

        Intent intent = getIntent();
        if (intent != null && intent.getData() != null){
            Bundle bundle = intent.getExtras();
            String filename = bundle.getString("file");
            if(filename != null) {
                Log.v(TAG,"Got filename: " + filename);

            }
        }
    }
}
