//Wi-Fi test and alarm test buttons
package com.samehf.alsalert

import android.graphics.Color
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.util.Log
import android.view.View
import android.widget.Button
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import androidx.fragment.app.Fragment
import com.google.firebase.firestore.FirebaseFirestore
import com.google.firebase.firestore.ListenerRegistration
import java.text.SimpleDateFormat
import java.util.Locale


class connectionFragment : Fragment(R.layout.fragment_connection) {

    // The device checks Firestore every 5 s and takes a few seconds to answer
    private val wifiTestTimeoutMs = 20000L

    private var listener : ListenerRegistration? = null
    private var listener2 : ListenerRegistration? = null
    private val db = FirebaseFirestore.getInstance()
    private val handler = Handler(Looper.getMainLooper())
    private var clickHandled = false


    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)

        // Find the button by its ID
        val wifiTestButton = view.findViewById<Button>(R.id.wifiTestButton)
        val wifiTestText = view.findViewById<TextView>(R.id.wifiTestText)
        val alarmTestButton = view.findViewById<Button>(R.id.alarmTestButton)
        val alarmStopButton = view.findViewById<Button>(R.id.alarmStopButton)

        val preferences = requireActivity().getSharedPreferences("LoginPrefs", AppCompatActivity.MODE_PRIVATE)
        val uid = preferences.getString("uid", null) ?: return

        alarmTestButton.setOnClickListener {
            db.collection("ALSAlert").document(uid)
                .update("startAlarm", "1")
        }

        alarmStopButton.setOnClickListener {
            db.collection("ALSAlert").document(uid)
                .update("stopAlarm", "1")
        }

        listener2 = db.collection("ALSAlert").document(uid)
            .addSnapshotListener { snapshot, exception ->
                if (exception != null) {
                    // Handle errors
                    return@addSnapshotListener
                }


                if (snapshot != null && snapshot.exists()) {
                    val lastConnectTime = snapshot.getTimestamp("lastConnection")
                    if (lastConnectTime != null) {
                        // Use the updated value of lastConnection here
                        // For example, set it to a TextView
                        val dateFormat = SimpleDateFormat("MM/dd/yyyy hh:mm:ss a", Locale.getDefault())
                        val lastConnectTimeFormatted = dateFormat.format(lastConnectTime.toDate())

                        wifiTestText.text = lastConnectTimeFormatted
                    } else {
                        // Handle the case when "lastConnection" field is null
                    }
                } else {
                    // Handle the case when document doesn't exist
                }
            }

        // Set a click listener for the button
        wifiTestButton.setOnClickListener {
            if(!clickHandled){
                clickHandled = true
                handler.postDelayed({
                    // No answer from the device: report it and let the user try again
                    wifiTestText.text = "Failed!"
                    clickHandled = false
                    listener?.remove()
                }, wifiTestTimeoutMs)
                wifiTestButton.setBackgroundColor(Color.RED)
                Log.d("connected", "here now1")
                db.collection("ALSAlert").document(uid)
                    .update("connected", "3")
                    .addOnSuccessListener {
                        // Handle success
                    }
                    .addOnFailureListener { e ->
                        // Handle failure
                    }

                // Handle the connection here
                listener = db.collection("ALSAlert").document(uid)
                    .addSnapshotListener { snapshot, exception ->
                        if (exception != null) {
                            // Handle errors
                            return@addSnapshotListener
                        }

                        val connected = snapshot?.getString("connected")
                        if (connected == "4") {
                            // The device answered the test
                            wifiTestButton.setBackgroundColor(Color.GREEN)
                            clickHandled = false
                            handler.removeCallbacksAndMessages(null)
                            listener?.remove()
                        }
                    }
            }
        }
    }

    override fun onDestroyView() {
        super.onDestroyView()
        handler.removeCallbacksAndMessages(null)
        clickHandled = false
        listener?.remove()
        listener2?.remove()
    }
}
