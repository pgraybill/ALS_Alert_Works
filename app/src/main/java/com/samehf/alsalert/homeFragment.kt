//This one should be for stopping the alarm and getting the notification and how long it has been since the last
package com.samehf.alsalert
import android.annotation.SuppressLint
import android.graphics.Typeface
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.view.Gravity
import android.view.View
import android.widget.Button
import android.widget.LinearLayout
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import androidx.fragment.app.Fragment
import com.google.firebase.Timestamp
import com.google.firebase.firestore.FirebaseFirestore
import com.google.firebase.firestore.ListenerRegistration
import java.text.SimpleDateFormat
import java.util.Locale

class homeFragment:Fragment(R.layout.fragment_home) {

    private var latestTimestamp: Timestamp? = null
    private val db = FirebaseFirestore.getInstance()
    private var listenerRegistration: ListenerRegistration? = null
    private lateinit var agoText : TextView
    private val handler = Handler(Looper.getMainLooper())
    private val updateRunnable = object : Runnable {
        override fun run() {
            updateDisplay()
            // Schedule the next update after 1 second
            handler.postDelayed(this, 1000)
        }
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)

        val preferences = requireActivity().getSharedPreferences("LoginPrefs", AppCompatActivity.MODE_PRIVATE)
        val uid = preferences.getString("uid", null)

        val alarmStopButton = view.findViewById<Button>(R.id.stopAlarmButton2)
        agoText = view.findViewById<TextView>(R.id.agoText)

        alarmStopButton.setOnClickListener {
            if (uid != null) {
                db.collection("ALSAlert").document(uid)
                    .update("stopAlarm", "1")
            }
        }
    }

    // Listening from onResume (not onViewCreated) so Home keeps updating
    // after the app has been in the background.
    override fun onResume() {
        super.onResume()
        startListening()
        handler.removeCallbacks(updateRunnable)
        handler.post(updateRunnable)
    }

    override fun onPause() {
        super.onPause()
        // Remove the snapshot listener when the fragment is paused
        listenerRegistration?.remove()
        listenerRegistration = null
        handler.removeCallbacks(updateRunnable)
    }

    private fun startListening() {
        val preferences = requireActivity().getSharedPreferences("LoginPrefs", AppCompatActivity.MODE_PRIVATE)
        val uid = preferences.getString("uid", null) ?: return

        val documentRef = db.collection("ALSAlert").document(uid)
        listenerRegistration = documentRef.addSnapshotListener { snapshot, exception ->
            if (exception != null) {
                // Handle errors
                return@addSnapshotListener
            }

            if (snapshot != null && snapshot.exists()) {
                // Retrieve the "LastNotification" field as an array of Timestamps (newest first)
                @Suppress("UNCHECKED_CAST")
                val timestampsList = snapshot.get("LastNotification") as? List<Timestamp>
                if (timestampsList != null) {
                    showAlarmHistory(timestampsList)
                }
            }
        }
    }

    private fun showAlarmHistory(timestampsList: List<Timestamp>) {
        val view = view ?: return
        // Reference to the LinearLayout container
        val container: LinearLayout = view.findViewById(R.id.container)

        container.removeAllViews()
        // Format for displaying timestamps
        val dateFormat = SimpleDateFormat("MM/dd/yyyy hh:mm:ss a", Locale.getDefault())

        // Iterate through the list of timestamps and create TextViews
        for (timestamp in timestampsList) {
            val textView = TextView(context)
            textView.text = dateFormat.format(timestamp.toDate())
            textView.textSize = 20f  // Set text size to 20sp
            textView.setTypeface(null, Typeface.BOLD)  // Set text to bold
            textView.gravity = Gravity.LEFT  // Center the text horizontally
            textView.setLineSpacing(8f, 1.0f)  // Adjust the first parameter for line spacing in pixels

            // Set padding (optional)
            textView.setPadding(0, 8, 0, 8)

            // Add the TextView to the LinearLayout container
            container.addView(textView)
        }

        val latest = timestampsList.firstOrNull() ?: return
        latestTimestamp = latest
        view.findViewById<TextView>(R.id.lastAlarmText).text = dateFormat.format(latest.toDate())
        updateDisplay()
    }

    @SuppressLint("SetTextI18n")
    fun updateDisplay() {
        val timestampTime = latestTimestamp?.toDate()?.time ?: return
        val currentTime = Timestamp.now().toDate().time
        val timeDifference = currentTime - timestampTime

        val days = timeDifference / (1000 * 60 * 60 * 24)
        val hours = timeDifference / (1000 * 60 * 60) % 24
        val minutes = timeDifference / (1000 * 60) % 60
        val seconds = timeDifference / 1000 % 60

        if(days.toInt() ==0){
            if(hours.toInt() ==0){
                if(minutes.toInt()==0){
                    if(seconds.toInt() == 1){
                        agoText.text = "${seconds} second ago"
                    } else {
                        agoText.text = "${seconds} seconds ago"
                    }

                } else if(minutes.toInt()==1) {
                    agoText.text = "${minutes} minute ago"
                } else {
                    agoText.text = "${minutes} minutes ago"
                }
            } else if(hours.toInt() ==1) {
                agoText.text = "${hours} hour ago"
            } else {
                agoText.text = "${hours} hours ago"
            }
        } else if (days.toInt() ==1){
            agoText.text = "${days} day ago"
        } else {
            agoText.text = "${days} days ago"
        }

    }
}
