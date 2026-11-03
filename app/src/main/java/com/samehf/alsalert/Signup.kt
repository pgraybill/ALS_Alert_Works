//Control user sign up
package com.samehf.alsalert

import android.annotation.SuppressLint
import android.content.Context
import android.content.Intent
import android.icu.util.TimeZone
import androidx.appcompat.app.AppCompatActivity
import android.os.Bundle
import android.util.Log
import android.view.View
import android.widget.Button
import android.widget.EditText
import android.widget.TextView
import android.widget.Toast
import com.google.firebase.auth.FirebaseAuth
import com.google.firebase.firestore.FirebaseFirestore
import com.google.firebase.messaging.FirebaseMessaging
class Signup : AppCompatActivity() {

    private lateinit var firebaseAuth: FirebaseAuth;

    @SuppressLint("MissingInflatedId", "SetTextI18n")
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_signup)

        val backbutton = findViewById<Button>(R.id.SBackButton)
        backbutton.setOnClickListener {
            val intent = Intent(this, MainActivity::class.java)
            startActivity(intent)
        }

        firebaseAuth = FirebaseAuth.getInstance()

        val submitSignup = findViewById<Button>(R.id.sSignupButton)
        val errorText = findViewById<TextView>(R.id.signupErrorText)

        submitSignup.setOnClickListener {
            val email = findViewById<EditText>(R.id.editSEmail).text.toString()
            val pass = findViewById<EditText>(R.id.editSPass).text.toString()
            val confirmPass = findViewById<EditText>(R.id.editSConfirmPass).text.toString()
            if (email.isNotEmpty() && pass.isNotEmpty() && confirmPass.isNotEmpty()) {
                if(pass == confirmPass) {
                    firebaseAuth.createUserWithEmailAndPassword(email, pass).addOnCompleteListener {
                        if (it.isSuccessful) {
//                            val intent = Intent(this, LoginActivity::class.java)
//                            startActivity(intent)
                            val preferences = getSharedPreferences("LoginPrefs", Context.MODE_PRIVATE)
                            val editor = preferences.edit()

                            val user = firebaseAuth.currentUser
                            val uid = user?.uid
                            // Save login information

                            // Save login information

                            editor.putString("email", email)
                            editor.putString("password", pass)
                            editor.putString("uid", uid)
                            editor.putString("step", "initial")
                            editor.apply()


                            // Get FCM token
                            FirebaseMessaging.getInstance().token.addOnCompleteListener { tokenTask ->
                                if (tokenTask.isSuccessful) {
                                    val fcmToken = tokenTask.result

                                    // Store FCM token in Firestore
                                    if (uid != null) {

                                        storeFCMTokenInFirestore(uid, fcmToken)
                                    }
                                }
                            }

//                        Toast.makeText(this, println(it.toString()), Toast.LENGTH_SHORT).show()
                            val intent = Intent(this, SetupWifi::class.java)
                            startActivity(intent)

                            //Toast.makeText(this, "Created successfully!", Toast.LENGTH_SHORT).show()
                            errorText.visibility = View.INVISIBLE
                        } else {
                            //Toast.makeText(this, it.exception.toString(), Toast.LENGTH_SHORT).show()
                            Log.d("Signup Error:", it.exception.toString())
                            if(it.exception.toString().contains("is already in use by another")){
                                errorText.text = "Email Already Used. Try a Different One"
                                errorText.visibility = View.VISIBLE
                            } else if (it.exception.toString().contains("Password should be at least")) {
                                errorText.text = "Password Must be at Least 6 Characters"
                                errorText.visibility = View.VISIBLE
                            } else if (it.exception.toString().contains("email address is badly")) {
                                errorText.text = "Email is Wrong"
                                errorText.visibility = View.VISIBLE
                            }
                        }
                    }
                } else {
                    //Toast.makeText(this, "Password does not match!", Toast.LENGTH_SHORT).show()
                    errorText.text = "Password Does Not Match"
                    errorText.visibility = View.VISIBLE
                }
            } else {
                //Toast.makeText(this, "Empty Inputs Are Not Allowed!", Toast.LENGTH_SHORT).show()
                errorText.text = "All Inputs Must Be Filled"
                errorText.visibility = View.VISIBLE
            }
        }
    }

    private fun storeFCMTokenInFirestore(uid: String, fcmToken: String?) {
        if (fcmToken != null) {
            val firestore = FirebaseFirestore.getInstance()

            // Reference to the "ALSAlert" collection and the document with UID
            val espCollection = firestore.collection("ALSAlert")
            val userDocument = espCollection.document(uid)

            // Check if the document exists
            userDocument.get().addOnCompleteListener { documentSnapshotTask ->
                if (documentSnapshotTask.isSuccessful) {
                    val documentSnapshot = documentSnapshotTask.result

                    if (documentSnapshot != null && documentSnapshot.exists()) {
                        // Document exists, update the existing document with the FCM token
                        userDocument.update(
                            "token", fcmToken,
                            "timezone", TimeZone.getDefault().getID(),
                            "notification", "0",
                            "stopAlarm", "0",
                            "startAlarm", "0",
                            "startCal", "0",
                        )
                            .addOnSuccessListener {
                                // Successfully updated the document
                            }
                            .addOnFailureListener { e ->
                                // Handle the error
                                // e.printStackTrace()
                            }


                    } else {
                        // Document doesn't exist, create a new document with the FCM token
                        val data = hashMapOf(
                            "token" to fcmToken,
                            "timezone" to TimeZone.getDefault().getID(),
                            "notification" to "0",
                            "stopAlarm" to "0",
                            "startAlarm" to "0",
                            "resetAll" to "0",
                            "startCal" to "0",

                        )
                        userDocument.set(data)
                            .addOnSuccessListener {
                                // Successfully created the document
                            }
                            .addOnFailureListener { e ->
                                // Handle the error
                                // e.printStackTrace()
                            }
                    }
                } else {
                    // Handle the error
                    // documentSnapshotTask.exception?.printStackTrace()
                }
            }
        }
    }
}