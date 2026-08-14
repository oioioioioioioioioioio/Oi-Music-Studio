package studio.oi.musiceditor;

import android.os.Build;
import android.os.Bundle;
import android.view.View;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;

import com.rmsl.juce.JuceActivity;

public final class OiJuceActivity extends JuceActivity
{
    @Override
    protected void onCreate (Bundle savedInstanceState)
    {
        super.onCreate (savedInstanceState);

        if (Build.VERSION.SDK_INT >= 28)
        {
            final WindowManager.LayoutParams attributes = getWindow().getAttributes();
            attributes.layoutInDisplayCutoutMode
                = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
            getWindow().setAttributes (attributes);
        }

        enterImmersiveMode();
    }

    @Override
    protected void onResume()
    {
        super.onResume();
        enterImmersiveMode();
    }

    @Override
    public void onWindowFocusChanged (boolean hasFocus)
    {
        super.onWindowFocusChanged (hasFocus);
        if (hasFocus)
            enterImmersiveMode();
    }

    @SuppressWarnings ("deprecation")
    private void enterImmersiveMode()
    {
        final Window window = getWindow();
        final View decorView = window.getDecorView();

        if (Build.VERSION.SDK_INT >= 30)
        {
            window.setDecorFitsSystemWindows (false);
            final WindowInsetsController controller = decorView.getWindowInsetsController();
            if (controller != null)
            {
                controller.hide (WindowInsets.Type.statusBars()
                                 | WindowInsets.Type.navigationBars());
                controller.setSystemBarsBehavior (
                    WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
            return;
        }

        decorView.setSystemUiVisibility (
              View.SYSTEM_UI_FLAG_LAYOUT_STABLE
            | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
            | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
            | View.SYSTEM_UI_FLAG_FULLSCREEN
            | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
            | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
    }
}
