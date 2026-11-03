//Email, password, and notification permission
package com.samehf.alsalert

import android.Manifest
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.media.RingtoneManager
import android.os.Build
import android.os.Bundle
import android.util.Log
import android.view.View
import android.widget.Button
import android.widget.TextView
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat
import androidx.fragment.app.Fragment
import com.google.firebase.FirebaseApp
import com.google.firebase.auth.FirebaseAuth
import kotlin.system.exitProcess


class MainActivity : AppCompatActivity() {

    private lateinit var firebaseAuth: FirebaseAuth;class connectionFragment : Fragment(R.layout.fragment_connection)

    // Declare the launcher at the top of your Activity/Fragment:
    private val requestPermissionLauncher = registerForActivityResult(
        ActivityResultContracts.RequestPermission(),
    ) { isGranted: Boolean ->
        if (isGranted) {
            // FCM SDK (and your app) can post notifications.
            Toast.makeText(this,"notification permission true", Toast.LENGTH_LONG)
        } else {
            Toast.makeText(this,"Notifications are off, so alarms can't reach this phone. Turn them on in Settings.", Toast.LENGTH_LONG).show()
        }
    }



    private fun askNotificationPermission() {
        // This is only necessary for API level >= 33 (TIRAMISU)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            if (ContextCompat.checkSelfPermission(this, Manifest.permission.POST_NOTIFICATIONS) ==
                PackageManager.PERMISSION_GRANTED
            ) {
                // FCM SDK (and your app) can post notifications.
                //Toast.makeText(this,"notification permission true", Toast.LENGTH_LONG)
            } else {
                // Directly ask for the permission
                requestPermissionLauncher.launch(Manifest.permission.POST_NOTIFICATIONS)
                //Toast.makeText(this,"notification permission false", Toast.LENGTH_LONG)
            }
        }
    }
    
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        askNotificationPermission()

//        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
//            if (checkSelfPermission(Manifest.permission.ACCESS_COARSE_LOCATION) != PackageManager.PERMISSION_GRANTED) {
//                requestPermissions(arrayOf(Manifest.permission.ACCESS_COARSE_LOCATION), 87)
//            }
//
//            if (checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION) != PackageManager.PERMISSION_GRANTED) {
//                requestPermissions(arrayOf(Manifest.permission.ACCESS_FINE_LOCATION), 87)
//            }
//        }

        FirebaseApp.initializeApp(this)
        firebaseAuth = FirebaseAuth.getInstance()

//        val soundUri = RingtoneManager.getDefaultUri(RingtoneManager.TYPE_ALARM)
        MyFirebaseMessagingService.ConstantFunction.createNotificationChannel(this)

        val preferences = getSharedPreferences("LoginPrefs", MODE_PRIVATE)
        val savedEmail = preferences.getString("email", null)
        val savedPassword = preferences.getString("password", null)

        val loginbutton = findViewById<Button>(R.id.loginButton)
        val signupbutton = findViewById<Button>(R.id.SignupButton)
        val loadingtext = findViewById<TextView>(R.id.loading)
        loadingtext.visibility = View.INVISIBLE

        if (savedEmail != null && savedPassword != null) {
            // Automatically log in using saved credentials
                loginbutton.visibility = View.GONE
                signupbutton.visibility = View.GONE
                loadingtext.visibility = View.VISIBLE
                firebaseAuth.signInWithEmailAndPassword(savedEmail, savedPassword).addOnCompleteListener {
                    if (it.isSuccessful) {
                        val step = preferences.getString("step", null)
                        if(step=="initial") {
                            val intent = Intent(this, SetupWifi::class.java)
                            startActivity(intent)
                        } else if(step=="complete"){
                            val intent = Intent(this, Home::class.java)
                            startActivity(intent)
                        } else if(step==null) {
                            val preferences = getSharedPreferences("LoginPrefs", Context.MODE_PRIVATE)
                            val editor = preferences.edit()
                            editor.putString("step", "initial")
                            editor.apply()
                            val intent = Intent(this, SetupWifi::class.java)
                            startActivity(intent)
                        }
                    } else {
                        Toast.makeText(this,"Error Logging In",Toast.LENGTH_LONG).show()
                    }
                    loginbutton.visibility = View.VISIBLE
                    signupbutton.visibility = View.VISIBLE
                    loadingtext.visibility = View.INVISIBLE
                }
        }



        loginbutton.setOnClickListener {
            val intent = Intent(this, Login::class.java)
            startActivity(intent)
        }


        signupbutton.setOnClickListener {
            val intent = Intent(this, Signup::class.java)
            startActivity(intent)
        }

    }

}

