package com.m.controller;

import androidx.annotation.Nullable;
import androidx.appcompat.app.AppCompatActivity;
import androidx.appcompat.widget.SwitchCompat;
import androidx.preference.PreferenceFragmentCompat;

import android.content.Context;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventCallback;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.hardware.usb.UsbDeviceConnection;
import android.hardware.usb.UsbManager;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.MotionEvent;
import android.view.View;
import android.widget.Button;
import android.widget.ProgressBar;
import android.widget.Switch;
import android.widget.TextView;

import com.hoho.android.usbserial.driver.UsbSerialDriver;
import com.hoho.android.usbserial.driver.UsbSerialPort;
import com.hoho.android.usbserial.driver.UsbSerialProber;

import org.w3c.dom.Text;

import java.io.IOException;
import java.util.List;

class MotorPacket {
    public int leftSpeed = 0, rightSpeed = 0;

    byte[] serialize() {
        return new byte[]{(byte) (leftSpeed & 0xFF), (byte) (rightSpeed & 0xFF)};
    }
}


class CustomSlider implements View.OnTouchListener {
    CustomSlider(View view) {
        view.setOnTouchListener(this);
        view.setBackgroundColor(0xFFFFFFFF);
    }

    public float client;
    float last;

    public static float byteClamp(float v) {
        return Math.min(Math.max(v, 0), 255);
    }

    @Override
    public boolean onTouch(View view, MotionEvent motionEvent) {
        switch (motionEvent.getAction()) {
            case MotionEvent.ACTION_DOWN:
                last = (int) motionEvent.getY();
                return true;
            case MotionEvent.ACTION_MOVE:
                float delta = (last - motionEvent.getY()) / view.getHeight() * 255 * 2;
                // if client outside boundaries caused by gyro still make it able return
                // to normal values, while slider code wont exceed boundaries
                float clientDelta = byteClamp(byteClamp(client) + delta) - client;
                client += clientDelta;
                last = motionEvent.getY();
                return true;
            case MotionEvent.ACTION_UP:
                view.performClick();
                break;
        }
        return false;
    }
}

class Preferences extends PreferenceFragmentCompat {
    @Override
    public void onCreatePreferences(@Nullable Bundle savedInstanceState, @Nullable String rootKey) {
        setPreferencesFromResource(R.xml.preferences, rootKey);
    }
}

class SettingsActivity extends AppCompatActivity {
}

public class MainActivity extends AppCompatActivity {
    MotorPacket motorPacket;
    UsbDeviceConnection connection;
    UsbSerialPort port;
    SwitchCompat connectedSwitch;
    Sensor sensor;

    SwitchCompat gyrosSwitch;
    SwitchCompat cruiseSwitch;
    float sideMove;

    void loadSensor() {
        gyrosSwitch = findViewById(R.id.gyros);
        gyrosSwitch.setOnCheckedChangeListener((c, b) -> sideMove = b ? 0 : sideMove);
        cruiseSwitch = findViewById(R.id.cruise);
        SensorManager manager = (SensorManager) getSystemService(Context.SENSOR_SERVICE);
        sensor = manager.getDefaultSensor(Sensor.TYPE_GYROSCOPE);

        manager.registerListener((new SensorEventCallback() {
            @Override
            public void onSensorChanged(SensorEvent event) {
                super.onSensorChanged(event);
                float moveAxis = event.values[0];
                float speedAxis = event.values[1];
                if (gyrosSwitch.isChecked()) {
                    // om te calibreren gyros uit,
                    // de sliders handmatig aanpassen,
                    // gyros aan.

                    if (!cruiseSwitch.isChecked()) {
                        left.client += speedAxis * 3;
                        right.client += speedAxis * 3;
                    }
                    sideMove += moveAxis;
                }
            }
        }), sensor, 10);
    }


    void sendMotorPacket() {
        try {
            port.write(motorPacket.serialize(), 0);
        } catch (IOException e) {
            throw new RuntimeException(e);
        }
    }

    TextView info;

    /**
     * @return is currently connected
     */
    boolean tryConnect() {
        if (port != null && port.isOpen()) return true;
        UsbManager manager = (UsbManager) getSystemService(Context.USB_SERVICE);
        List<UsbSerialDriver> availableDrivers = UsbSerialProber.getDefaultProber().findAllDrivers(manager);
        if (availableDrivers.isEmpty()) return false;

        // Open a connection to the first available driver.
        UsbSerialDriver driver = availableDrivers.get(0);
        connection = manager.openDevice(driver.getDevice());
        if (connection == null) return false;

        port = driver.getPorts().get(0);
        try {
            port.open(connection);
            port.setParameters(115200, 8, UsbSerialPort.STOPBITS_1, UsbSerialPort.PARITY_NONE);
        } catch (IOException e) {
            connection.close();
            return false;
        }
        connectedSwitch.setChecked(true);
        return true;
    }

    CustomSlider left, right;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        loadSensor();

        motorPacket = new MotorPacket();

        left = new CustomSlider(findViewById(R.id.left));
        right = new CustomSlider(findViewById(R.id.right));
        connectedSwitch = findViewById(R.id.connected);
        connectedSwitch.setEnabled(false);

        ProgressBar leftMotor = findViewById(R.id.motorleft);
        ProgressBar rightMotor = findViewById(R.id.motorright);

        Button connectButton = findViewById(R.id.connect);
        connectButton.setOnClickListener(v -> tryConnect());

        connectButton.performClick();

        Handler handler = new Handler(Looper.myLooper());
        handler.post(new Runnable() {
            @Override
            public void run() {
                motorPacket.leftSpeed = (int) CustomSlider.byteClamp(CustomSlider.byteClamp(left.client) + sideMove);
                motorPacket.rightSpeed = (int) CustomSlider.byteClamp(CustomSlider.byteClamp(right.client) - sideMove);
                if (port != null && port.isOpen()) sendMotorPacket();
                runOnUiThread(() -> {
                    leftMotor.setProgress(motorPacket.leftSpeed);
                    rightMotor.setProgress(motorPacket.rightSpeed);
                });
                handler.postDelayed(this, 1);
            }
        });
    }

    @Override
    protected void onDestroy() {
        if (port != null && port.isOpen()) {
            try {
                port.close();
                connection.close();
            } catch (IOException e) {
                throw new RuntimeException(e);
            }
        }
        super.onDestroy();
    }
}