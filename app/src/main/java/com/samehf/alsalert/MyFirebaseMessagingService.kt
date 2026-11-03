//Notification. Sound?
package com.samehf.alsalert

import android.annotation.SuppressLint
import android.app.NotificationChannel
import android.app.NotificationManager
import android.content.ContentResolver
import android.content.Context
import android.graphics.BitmapFactory
import android.graphics.Color
import android.icu.util.TimeZone
import android.media.AudioAttributes
import android.net.Uri
import android.os.Build
import android.util.Log
import androidx.core.app.NotificationCompat
import androidx.core.app.NotificationManagerCompat
import com.google.firebase.auth.FirebaseAuth
import com.google.firebase.firestore.FirebaseFirestore
import com.google.firebase.messaging.FirebaseMessagingService
import com.google.firebase.messaging.RemoteMessage


class MyFirebaseMessagingService : FirebaseMessagingService() {
    private val TAG = "MyFirebaseMsgService"
    private lateinit var firebaseAuth: FirebaseAuth

    // Only runs while the app is open. When it is closed or in the background,
    // Android shows the notification itself on the channel the message names.
    @SuppressLint("MissingPermission")
    override fun onMessageReceived(remoteMessage: RemoteMessage) {
        Log.d(TAG, "From: ${remoteMessage.from}")
        ConstantFunction.createNotificationChannel(this)

        val title = remoteMessage.notification?.title
        val body = remoteMessage.notification?.body
        showNotification(this, title, body, ConstantFunction.ALERT_CHANNEL_ID)
    }

    override fun onNewToken(token: String) {
        // If you want to send messages to this application instance or
        // manage this apps subscriptions on the server side, send the
        // FCM registration token to your app server.
        firebaseAuth = FirebaseAuth.getInstance()
        val user = firebaseAuth.currentUser
        val uid = user?.uid.toString()

        if (token != null && uid != "null") {
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
                        userDocument.update("token", token,
                            "timezone", TimeZone.getDefault().getID(),
                            "notification", "0")
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
                            "token" to token,
                            "timezone" to TimeZone.getDefault().getID()

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

    object ConstantFunction {

        // Must match default_notification_channel_id in strings.xml and functions/index.js.
        // Android never changes the sound of an existing channel, so the alarm sound
        // needed a new ID; the old one ("myID123") had no custom sound.
        const val ALERT_CHANNEL_ID = "als_alerts"
        private const val OLD_CHANNEL_ID = "myID123"

        // Called at app start so the channel exists before the first alarm arrives,
        // even if the app is in the background when it does.
        fun createNotificationChannel(context: Context) {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                val notificationManager =
                    context.getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager

                val soundUri = Uri.parse(ContentResolver.SCHEME_ANDROID_RESOURCE + "://" + context.packageName + "/" + R.raw.alert2)
                val audioAttributes = AudioAttributes.Builder()
                    .setUsage(AudioAttributes.USAGE_ALARM)
                    .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION)
                    .build()

                val channel = NotificationChannel(
                    ALERT_CHANNEL_ID,
                    "ALS Alerts",
                    NotificationManager.IMPORTANCE_HIGH
                )
                channel.description = "Patient alarms, loose electrodes and device offline alerts"
                channel.lightColor = Color.RED
                channel.enableLights(true)
                channel.enableVibration(true)
                channel.setSound(soundUri, audioAttributes)
                notificationManager.createNotificationChannel(channel)
                notificationManager.deleteNotificationChannel(OLD_CHANNEL_ID)
            }
        }
    }

    @SuppressLint("MissingPermission")
    fun showNotification(context: Context, title: String?, body: String?, channelId: String) {
        val notificationManager = NotificationManagerCompat.from(context)

        val largeicon = BitmapFactory.decodeResource(context.resources, R.drawable.warningsymbol)

        val notificationBuilder = NotificationCompat.Builder(context, channelId)
            .setContentTitle(title)
            .setContentText(body)
            .setStyle(NotificationCompat.BigTextStyle().bigText(body))
            .setPriority(NotificationCompat.PRIORITY_MAX)  // Set priority to high for heads-up notification
            .setSmallIcon(R.drawable.warningsymbol)
            .setLargeIcon(largeicon)
            .setAutoCancel(true)

        // A separate ID for each message, so a new alert doesn't replace an unread alarm
        notificationManager.notify(System.currentTimeMillis().toInt(), notificationBuilder.build())
    }
}
