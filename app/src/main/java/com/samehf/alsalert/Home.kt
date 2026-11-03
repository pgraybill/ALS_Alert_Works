//This one should be for creating the three app homepages
package com.samehf.alsalert

import android.content.ActivityNotFoundException
import android.content.Intent
import android.net.Uri
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity
import android.os.Bundle
import android.os.PowerManager
import android.provider.Settings
import androidx.fragment.app.Fragment
import com.google.android.material.navigation.NavigationBarView

class Home : AppCompatActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_home)

//        FirebaseMessaging.getInstance().token.addOnCompleteListener { task ->
//            if (task.isSuccessful) {
//                val token = task.result
//                // Handle the token, e.g., send it to your server
//            } else {
//                // Handle the error
//                // task.exception?.printStackTrace()
//            }
//        }

        //val intent = Intent(this, connection::class.java)
        //startActivity(intent)

        val homeFragment = homeFragment()
        val connectionFragment = connectionFragment()
        val settingsFragment = settingsFragment()

        //calibration
        val calibrationFragment = calibrationFragment()

        setCurrentFragment(homeFragment)

        var bottomNavigationView = findViewById<NavigationBarView>(R.id.bottomNavigationView)

        bottomNavigationView.setOnItemSelectedListener {
            when (it.itemId) {
                R.id.home -> setCurrentFragment(homeFragment)
                R.id.connection -> setCurrentFragment(connectionFragment)
                R.id.settings -> setCurrentFragment(settingsFragment)

                //calibrationFragment
                R.id.calibration -> setCurrentFragment(calibrationFragment)

            }
            true
        }

        askToIgnoreBatteryOptimization()
    }

    // Some phones delay or block push notifications to apps under battery optimization.
    // Asks once; the setting can still be changed later in Android's settings.
    private fun askToIgnoreBatteryOptimization() {
        val powerManager = getSystemService(POWER_SERVICE) as PowerManager
        val preferences = getSharedPreferences("LoginPrefs", MODE_PRIVATE)
        if (powerManager.isIgnoringBatteryOptimizations(packageName) ||
            preferences.getBoolean("batteryPromptShown", false)) {
            return
        }
        preferences.edit().putBoolean("batteryPromptShown", true).apply()

        AlertDialog.Builder(this)
            .setTitle("Allow alarms to arrive on time")
            .setMessage("Battery saving can delay alarm notifications. On the next screen, allow ALS Alert to run in the background.")
            .setPositiveButton("Continue") { _, _ ->
                try {
                    startActivity(Intent(Settings.ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS, Uri.parse("package:$packageName")))
                } catch (e: ActivityNotFoundException) {
                    startActivity(Intent(Settings.ACTION_IGNORE_BATTERY_OPTIMIZATION_SETTINGS))
                }
            }
            .setNegativeButton("Not now", null)
            .show()
    }

    private fun setCurrentFragment(fragment: Fragment) =
        supportFragmentManager.beginTransaction().apply {
            replace(R.id.flFragment, fragment)
            commit()
        }
}
