package com.samehf.alsalert

import android.os.Bundle
import androidx.fragment.app.Fragment
import android.view.View
import android.widget.Button
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import com.google.firebase.firestore.FirebaseFirestore

class calibrationFragment : Fragment(R.layout.fragment_calibration) {

    private val db = FirebaseFirestore.getInstance()

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)

        //calibration
        val startCalButton = view.findViewById<Button>(R.id.startCalButton)

        val preferences =
            requireActivity().getSharedPreferences("LoginPrefs", AppCompatActivity.MODE_PRIVATE)
        val uid = preferences.getString("uid", null) ?: return

        //calibration
        startCalButton.setOnClickListener {
            db.collection("ALSAlert").document(uid)
                .update("startCal", "1")
            Toast.makeText(context, "The device will start calibrating within a few seconds", Toast.LENGTH_LONG).show()
        }
    }
}
