//All about resetall, button and the email/pass it resets
package com.samehf.alsalert

import android.annotation.SuppressLint
import android.content.Context
import android.content.Intent
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.view.MotionEvent
import android.view.View
import android.widget.Button
import android.widget.CheckBox
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import androidx.fragment.app.Fragment
import com.google.firebase.firestore.FirebaseFirestore


class settingsFragment:Fragment(R.layout.fragment_settings) {

    private var handler: Handler? = null
    private var longPressRunnable: Runnable? = null
    private val longPressDuration = 5000L // 5 seconds
    private val db = FirebaseFirestore.getInstance()

    @SuppressLint("ClickableViewAccessibility")
    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)

        val button = view.findViewById<Button>(R.id.resetAllButton)
        val emailText = view.findViewById<TextView>(R.id.showEmailText)
        val passText = view.findViewById<TextView>(R.id.showPassText)
        val passBox = view.findViewById<CheckBox>(R.id.showPassBox)

        val preferences = requireActivity().getSharedPreferences("LoginPrefs", AppCompatActivity.MODE_PRIVATE)
        val savedEmail = preferences.getString("email", null)
        val savedPassword = preferences.getString("password", null)

        emailText.text = savedEmail
        passText.text = "********"

        passBox.setOnClickListener {
            if(passBox.isChecked) {
                passText.text = savedPassword
            } else {
                passText.text = "********"
            }
        }


        button.setOnTouchListener { v, event ->
            when (event.action) {
                MotionEvent.ACTION_DOWN -> {
                    startTimer()
                }
                MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                    cancelTimer()
                }
            }
            true
        }
    }

    private fun startTimer() {
        handler = Handler(Looper.getMainLooper())
        longPressRunnable = Runnable {
            // Your long press action here
            // This code will execute after 3 seconds of pressing the button
            // Replace the code inside this block with your desired action
            //Log.d("test123","123123")
            val preferences = requireActivity().getSharedPreferences("LoginPrefs", AppCompatActivity.MODE_PRIVATE)
            var uid = preferences.getString("uid", null)

            if (uid != null) {
                db.collection("ALSAlert").document(uid)
                    .update("resetAll", "1")
            }

            val editor = preferences.edit()
            editor.putString("email", null)
            editor.putString("password", null)
            editor.putString("uid", null)
            editor.putString("step", "initial")
            editor.apply()


            val intent = Intent(activity, MainActivity::class.java)
            intent.flags = Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TASK
            startActivity(intent)
            //Toast.makeText(context,"Reset Succesful",Toast.LENGTH_LONG).show()
        }
        handler?.postDelayed(longPressRunnable!!, longPressDuration)
    }

    private fun cancelTimer() {
        handler?.removeCallbacks(longPressRunnable!!)
    }

}