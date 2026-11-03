/**
 * Import function triggers from their respective submodules:
 *
 * const {onCall} = require("firebase-functions/v2/https");
 * const {onDocumentWritten} = require("firebase-functions/v2/firestore");
 *
 * See a full list of supported triggers at https://firebase.google.com/docs/functions
 */

const functions = require('firebase-functions');
const admin = require('firebase-admin');
admin.initializeApp();

const firestore = admin.firestore();

exports.sendNotification = functions.firestore
  .document('ALSAlert/{documentId}')
  .onUpdate((change, context) => {
    const newValue = change.after.data();
    const previousValue = change.before.data();

    if((newValue.lastConnectiontemp === "now") && (previousValue.lastConnectiontemp !== "now")) {
        const currtime2 = new Date();

        return change.after.ref.update({ 
          lastConnection: currtime2,
          lastConnectiontemp: 0,
        })
          .catch(error => {
            console.error('Error updating document:', error);
            return null;
          });
    }

    // Check if the notification variable changed to "1" (string) or 1 (number)
    if ((newValue.notification === "1" || newValue.notification === 1) &&
        (previousValue.notification !== "1" && previousValue.notification !== 1)) {
      
        const currtime2 = new Date();
        const timezone = newValue.timezone;
        
        const currentLastNotification = newValue.LastNotification || [];

        // Prepend the new timestamp to the array
        const newLastNotification = [currtime2, ...currentLastNotification];

       // Set notification variable to "0" (string) or 0 (number)
       
       return change.after.ref.update({ 
        notification: "0",
        // LastNotification: admin.firestore.FieldValue.serverTimestamp() 
        // LastNotification: admin.firestore.FieldValue.arrayUnion(currtime2)
        LastNotification: newLastNotification
      })
        .then(() => {
          // Send notification to the phone using the token
          const phoneToken = newValue.token;
          sendNotificationToPhone(phoneToken, timezone);

          return null;
        })
        .catch(error => {
          console.error('Error updating document:', error);
          return null;
        });
    } else {
      // No need to send a notification
      return null;
    }
  });

function sendNotificationToPhone(phoneToken, timezone) {
  // Implement your code to send notifications here
  // You can use Firebase Cloud Messaging (FCM) or any other notification service
  // Example using FCM:
  const currtime = new Date().toLocaleTimeString("en-US", {timeZone: timezone});
  // const payload = {
  //   // notification: {
  //   // title: 'ALS Alert TRIGGERED!',
  //   // body: 'Patient is calling for help! \n' + currtime,
  //   // },
  //   android: {
  //     priority: 'high',
  //     notification: {
  //       title: 'ALS Alert!',
  //       body: 'Patient is calling for help! \n' + currtime,
  //       icon: 'warningsymbol',
  //       sound: 'alert2.mp3',
  //       notification_priority: 'PRIORITY_HIGH',
  //       // channel_id: 'myID123',
  //       visibility: "public",

      
  //     }
  //   },
  //   token: phoneToken,
  // };

  const payload = {
      "notification": {
        "title": 'ALS Alert TRIGGERED!',
        "body": 'Patient is calling for help! \n' + currtime,
      },
      "android": {
        "priority": "high",
        "notification": {
          "icon": "warningsymbol",
          "sound": "alert2"
        }
      },
      "token": phoneToken,
  }
  

  admin.messaging().send(payload)
    .then(response => {
      console.log('Notification sent successfully:', response);
      return null;
    })
    .catch(error => {
      console.error('Error sending notification:', error);
      return null;
    });
}
// to run it: "firebase deploy --only functions"