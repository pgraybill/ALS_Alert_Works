//About logging into the app
package com.samehf.alsalert

import android.content.Context
import android.content.Intent
import android.icu.util.TimeZone
import android.os.Bundle
import android.util.Log
import android.view.View
import android.widget.Button
import android.widget.EditText
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import com.google.firebase.auth.FirebaseAuth
import com.google.firebase.firestore.FirebaseFirestore
import com.google.firebase.messaging.FirebaseMessaging

class Login : AppCompatActivity() {
    private lateinit var firebaseAuth: FirebaseAuth;
// ...
// Initialize Firebase Auth
private val db = FirebaseFirestore.getInstance()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_login)

        firebaseAuth = FirebaseAuth.getInstance()

        val backbutton = findViewById<Button>(R.id.LBackButton)
        backbutton.setOnClickListener {
            val intent = Intent(this, MainActivity::class.java)
            startActivity(intent)
        }


        val submitLogin = findViewById<Button>(R.id.LLoginButton)
        val errorText = findViewById<TextView>(R.id.loginErrorText)



        submitLogin.setOnClickListener {
            val email = findViewById<EditText>(R.id.editLEmail).text.toString()
            val pass = findViewById<EditText>(R.id.editLPassword).text.toString()
            if (email.isNotEmpty() && pass.isNotEmpty()) {
                firebaseAuth.signInWithEmailAndPassword(email, pass).addOnCompleteListener {
                    if (it.isSuccessful) {
                        errorText.visibility = View.INVISIBLE
//                        val intent = Intent(this, MainActivity::class.java)
//                        startActivity(intent)
                        val user = firebaseAuth.currentUser
                        val uid = user?.uid
                        // Save login information

                        // Save login information
                        val preferences = getSharedPreferences("LoginPrefs", Context.MODE_PRIVATE)
                        val editor = preferences.edit()

                        val savedEmail = preferences.getString("email", null)
                        val savedPassword = preferences.getString("password", null)
                        if((savedEmail!=email) || (savedPassword!=pass)) {
                            editor.putString("step", "initial")
                            if (uid != null) {
                                db.collection("ALSAlert").document(uid)
                                    .update("resetAll", "1")
                            }
                        }

                        editor.putString("email", email)
                        editor.putString("password", pass)
                        editor.putString("uid", uid)
                        //editor.putString("step", "initial")
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
                        //val intent = Intent(this, Home::class.java)

                        val step = preferences.getString("step", null)
                        Log.d("step", step.toString())
                        if(step=="initial") {
                            val intent = Intent(this, SetupWifi::class.java)
                            startActivity(intent)
                        } else if(step=="complete"){
                            val intent = Intent(this, Home::class.java)
                            startActivity(intent)
                        } else {
                            val intent = Intent(this, SetupWifi::class.java)
                            startActivity(intent)
                        }
                    } else {
                        //Toast.makeText(this, it.exception.toString(), Toast.LENGTH_SHORT).show()
                        Log.d("Login Error:", it.exception.toString())
                        if(it.exception.toString().contains("The supplied auth credential is incorrect")) {
                            errorText.text = "Wrong Information"
                            errorText.visibility = View.VISIBLE
                        }
                    }
                }
            } else {
                //Toast.makeText(this, "Empty Inputs Are Not Allowed!", Toast.LENGTH_SHORT).show()
                errorText.text = "All Fields Must be Filled"
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