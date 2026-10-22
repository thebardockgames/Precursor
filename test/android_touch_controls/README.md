# Android touch controls (AI-assisted)

The APK overlay publishes two analog sticks and PS2 buttons to an SDL virtual
gamepad. Pointer IDs (not pointer indices) own controls; pause, loss of focus,
resize, cancellation and hiding the overlay release every input.

Run the platform-independent state regression with the Android Studio JDK:

```powershell
& 'C:/Program Files/Android/Android Studio/jbr/bin/javac.exe' -encoding UTF-8 -d out/touch-controls-test scripts/android-port/apk/app/src/main/java/com/opengoalandroid/jak1/TouchControlsState.java test/android_touch_controls/TouchControlsStateTest.java
& 'C:/Program Files/Android/Android Studio/jbr/bin/java.exe' -cp out/touch-controls-test com.opengoalandroid.jak1.TouchControlsStateTest
```

This covers every button, four simultaneous pointers, stick ownership, diagonal
normalization, drag-off release, duplicate button contacts and cancellation.
It does not replace a device test of Android MotionEvents, JNI and SDL mapping.
On device, verify Start opens the menu, the D-pad changes selection and cross
confirms; in gameplay verify both sticks plus jump and shoulders simultaneously.
Use the top-center OCULTAR/MANDO toggle to hide/show controls during cinematics.

The FPS readout remains visible when controls are hidden. It counts native SDL
buffer swaps, sampled once per second, not Android UI refreshes. Samples are also
logged under `OpenGOALPerf`. Verify immersive mode after resuming and after an edge
swipe; system bars should hide again and no controls should remain pressed.

The APK CMake option `OPENGOAL_ANDROID_OPTIMIZE` defaults to ON (-O2 with debug
symbols/assertions retained). Turn it OFF only when unoptimized stepping is needed.
The audio quantum remains 480 frames at 48 kHz: increasing it to 2048 introduced
coarser sound-synchronized animation updates and the user reported more lag.
