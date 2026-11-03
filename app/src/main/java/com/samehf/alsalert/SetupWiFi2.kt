//finishing setup and making the user
package com.samehf.alsalert

import android.annotation.SuppressLint
import android.content.Context
import android.content.Intent
import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkCapabilities
import android.net.Uri
import android.os.Bundle
import android.os.CountDownTimer
import android.os.Handler
import android.os.Looper
import android.util.Log
import android.view.View
import android.widget.Button
import android.widget.EditText
import android.widget.ProgressBar
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import com.google.firebase.FirebaseApp
import com.google.firebase.firestore.FirebaseFirestore
import com.google.firebase.firestore.ListenerRegistration
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.GlobalScope
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.net.HttpURLConnection
import java.net.URL

class SetupWiFi2 : AppCompatActivity() {

    // The ESP32 has to restart, join Wi-Fi (up to 15 s) and sign in to Firebase,
    // and the phone has to get back on the internet to see the result.
    private val setupTimeoutMs = 60000L
    private val httpTimeoutMs = 10000

    private lateinit var progressBar: ProgressBar
    private var countDownTimer: CountDownTimer? = null
    private val db = FirebaseFirestore.getInstance()
    private var listener: ListenerRegistration? = null
    private val timeoutHandler = Handler(Looper.getMainLooper())
    private var finished = false

    @SuppressLint("MissingInflatedId")
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_setup_wi_fi2)

        progressBar = findViewById(R.id.progressBar)
        val submit = findViewById<Button>(R.id.submitButton)
        val wifiname = findViewById<EditText>(R.id.wifiName)
        val wifipass = findViewById<EditText>(R.id.wifiPass)

        val preferences = getSharedPreferences("LoginPrefs", MODE_PRIVATE)
        val savedEmail = preferences.getString("email", null)
        val savedPassword = preferences.getString("password", null)
        val uid = preferences.getString("uid", null)
        val step = preferences.getString("step", null)

        if(step=="complete") {
            val intent = Intent(this, Home::class.java)
            startActivity(intent)
        }


        submit.setOnClickListener {
            submit.visibility = View.GONE
            wifiname.visibility = View.GONE
            wifipass.visibility = View.GONE
            progressBar.visibility = View.VISIBLE
            finished = false

            // Every value is encoded so characters like & # + % in a password survive.
            // The API key and project ID tell the device which Firebase project to use.
            val options = FirebaseApp.getInstance().options
            val url = "http://192.168.4.1/" +
                    "?ssid=${Uri.encode(wifiname.text.toString())}" +
                    "&password=${Uri.encode(wifipass.text.toString())}" +
                    "&email=${Uri.encode(savedEmail.orEmpty())}" +
                    "&epassword=${Uri.encode(savedPassword.orEmpty())}" +
                    "&apikey=${Uri.encode(options.apiKey)}" +
                    "&projectid=${Uri.encode(options.projectId.orEmpty())}"
            sendToDevice(url)

            handleProgressBar()
            timeoutHandler.postDelayed({ setupFailed() }, setupTimeoutMs)

            if (uid != null) {
                listener = db.collection("ALSAlert").document(uid)
                    .addSnapshotListener { snapshot, exception ->
                        if (exception != null) {
                            // Handle errors
                            return@addSnapshotListener
                        }

                        if (snapshot != null && snapshot.exists() && !finished) {
                            val connected = snapshot.getString("connected")
                            if (connected == "1") {

                                // If "connected" is set to "1", stop the progress and handle the connection
                                progressBar.progress = progressBar.max

                                db.collection("ALSAlert").document(uid)
                                    .update("connected", "2")
                                // Handle the connection here
                                val editor = getSharedPreferences("LoginPrefs", MODE_PRIVATE).edit()
                                editor.putString("step", "complete")
                                editor.putString("wifistatus", null)
                                editor.apply()

                                success()

                            }
                        }
                    }

            }

        }



    }

    fun success() {
        stopWaiting()
        //This is needed to avoid issues with closing and opening to wrong layout
        val intent = Intent(this, Home::class.java)
        intent.flags = Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TASK
        startActivity(intent)
    }

    private fun setupFailed() {
        if (finished) return
        stopWaiting()
        val editor = getSharedPreferences("LoginPrefs", Context.MODE_PRIVATE).edit()
        editor.putString("wifistatus", "failed")
        editor.apply()
        goback()
    }

    private fun stopWaiting() {
        finished = true
        timeoutHandler.removeCallbacksAndMessages(null)
        countDownTimer?.cancel()
        listener?.remove()
        listener = null
    }

    fun goback() {
        val intent2 = Intent(this, SetupWifi::class.java)
        startActivity(intent2)
        finish()
    }

    fun handleProgressBar() {
        progressBar = findViewById(R.id.progressBar)
        progressBar.max = 500

        countDownTimer = object : CountDownTimer(setupTimeoutMs, 100) {
            override fun onTick(millisUntilFinished: Long) {
                val progress = 500 - (millisUntilFinished * 500 / setupTimeoutMs).toInt()
                progressBar.progress = progress
            }

            override fun onFinish() {
                // Progress bar reached 100%
                progressBar.progress = progressBar.max
            }
        }

        // Start the countdown timer
        countDownTimer?.start()
    }


    override fun onDestroy() {
        super.onDestroy()
        // Cancel the timers and listener to prevent memory leaks
        stopWaiting()
    }

    // The "ALS Alert" network has no internet, so Android sends normal traffic over
    // cellular instead and 192.168.4.1 can't be reached. Sending this one request on the
    // Wi-Fi network directly fixes that and leaves Firestore on the phone's internet.
    @Suppress("DEPRECATION")
    private fun findWifiNetwork(): Network? {
        val connectivityManager = getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager
        return connectivityManager.allNetworks.firstOrNull { network ->
            connectivityManager.getNetworkCapabilities(network)
                ?.hasTransport(NetworkCapabilities.TRANSPORT_WIFI) == true
        }
    }

    private fun sendToDevice(url: String) {
        GlobalScope.launch(Dispatchers.IO) {
            val wifiNetwork = findWifiNetwork()
            val connection = try {
                if (wifiNetwork == null) throw IllegalStateException("Phone is not on Wi-Fi")
                val opened = wifiNetwork.openConnection(URL(url)) as HttpURLConnection
                opened.requestMethod = "GET"
                opened.connectTimeout = httpTimeoutMs
                opened.readTimeout = httpTimeoutMs
                opened.connect()
                opened
            } catch (e: Exception) {
                // The device can't be reached, so there is nothing to wait for.
                Log.d("SetupWiFi2", "Could not reach device: $e")
                withContext(Dispatchers.Main) {
                    Toast.makeText(this@SetupWiFi2, "Could not reach the device. Is this phone connected to \"ALS Alert\" Wi-Fi?", Toast.LENGTH_LONG).show()
                    setupFailed()
                }
                return@launch
            }

            // The device restarts as soon as it has the settings, so a cut-off reply
            // doesn't mean failure. The Firestore listener decides the result.
            try {
                val response = connection.inputStream.bufferedReader().use { it.readText() }
                Log.d("Response", response)
            } catch (e: Exception) {
                Log.d("SetupWiFi2", "Reply from device cut off: $e")
            } finally {
                connection.disconnect()
            }
        }
    }
}
