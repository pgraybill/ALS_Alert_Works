// ALS Alert Cloud Functions
//
// 1. onDeviceEvent:  a new ALSAlert/{uid}/events/{id} doc from the ESP32
//                    (alarm or loose electrode) -> push notification to the caregiver.
// 2. checkDeviceOnline: every minute, push "Device offline" when the device's
//                    lastSeen heartbeat is too old, and "back online" when it returns.
//
// See functions/README.md for deployment.

const { setGlobalOptions } = require("firebase-functions/v2");
const { onDocumentCreated } = require("firebase-functions/v2/firestore");
const { onSchedule } = require("firebase-functions/v2/scheduler");
const logger = require("firebase-functions/logger");
const { initializeApp } = require("firebase-admin/app");
const { getFirestore, Timestamp } = require("firebase-admin/firestore");
const { getMessaging } = require("firebase-admin/messaging");

// Must match your Firestore location. Default database location "nam5" (United States)
// works with us-central1. For other locations see functions/README.md.
const REGION = "us-central1";

// The device writes lastSeen every 20 s. Three missed heartbeats = offline.
const OFFLINE_AFTER_MS = 60 * 1000;

// Give up retrying an event push after this long (the alarm is no longer useful news).
const EVENT_MAX_AGE_MS = 60 * 60 * 1000;

// How many alarm times to keep in LastNotification (shown on the app's Home screen).
const MAX_ALARM_HISTORY = 20;

// Notification channel created by the app at startup (MainActivity / MyFirebaseMessagingService).
const ANDROID_CHANNEL_ID = "myID123";

setGlobalOptions({ region: REGION, maxInstances: 2 });
initializeApp();
const db = getFirestore();

const EVENT_TEXT = {
  alarm: {
    patient: { title: "ALS Alert: Patient needs help", body: "The patient triggered the alarm." },
    remote: { title: "ALS Alert: Test alarm", body: "An alarm was started from the app." },
  },
  electrode_left: {
    title: "ALS Alert: Left electrode loose",
    body: "The device can't detect alarms reliably until the electrode is reattached.",
  },
  electrode_right: {
    title: "ALS Alert: Right electrode loose",
    body: "The device can't detect alarms reliably until the electrode is reattached.",
  },
};

// Sends one high-priority push. Returns false if the user has no token or the
// token is no longer valid; throws on other (possibly temporary) errors.
async function sendPush(uid, token, title, body, data) {
  if (!token) {
    logger.warn("No FCM token stored for user, push not sent", { uid, title });
    return false;
  }
  try {
    await getMessaging().send({
      token,
      // A notification payload lets Android show it even when the app is closed.
      notification: { title, body },
      data: Object.fromEntries(Object.entries(data).map(([k, v]) => [k, String(v)])),
      android: {
        priority: "high",
        notification: { channelId: ANDROID_CHANNEL_ID, priority: "max", defaultSound: true },
      },
    });
    logger.info("Push sent", { uid, title });
    return true;
  } catch (err) {
    if (err.code === "messaging/registration-token-not-registered" ||
        err.code === "messaging/invalid-registration-token") {
      logger.warn("FCM token is no longer valid; the app must sign in again", { uid });
      return false;
    }
    throw err;
  }
}

exports.onDeviceEvent = onDocumentCreated(
  { document: "ALSAlert/{uid}/events/{eventId}", retry: true },
  async (event) => {
    const snap = event.data;
    if (!snap) return;
    const evt = snap.data();
    const { uid } = event.params;

    if (evt.notifiedAt) return; // Already handled (functions can be delivered twice)

    // When it actually happened: upload time minus time spent queued on the device.
    const uploadedAt = evt.at instanceof Timestamp ? evt.at.toMillis() : snap.createTime.toMillis();
    const happenedAt = uploadedAt - (Number(evt.delayMs) || 0);

    if (Date.now() - uploadedAt > EVENT_MAX_AGE_MS) {
      logger.warn("Event too old, not retrying", { uid, eventId: event.params.eventId });
      return;
    }

    let text;
    if (evt.type === "alarm") {
      text = EVENT_TEXT.alarm[evt.source === "remote" ? "remote" : "patient"];
    } else {
      text = EVENT_TEXT[evt.type];
    }
    if (!text) {
      logger.error("Unknown event type", { uid, type: evt.type });
      return;
    }

    const userRef = db.collection("ALSAlert").doc(uid);
    const userSnap = await userRef.get();
    const token = userSnap.get("token");

    let body = text.body;
    const delayMin = Math.round((Number(evt.delayMs) || 0) / 60000);
    if (delayMin >= 1) body += ` (happened ${delayMin} min ago while the device was offline)`;

    await sendPush(uid, token, text.title, body, {
      type: evt.type,
      source: evt.source || "patient",
      happenedAt,
    });

    // Patient alarms go into the Home screen's history (newest last, capped).
    if (evt.type === "alarm" && evt.source !== "remote") {
      await db.runTransaction(async (tx) => {
        const doc = await tx.get(userRef);
        const history = doc.get("LastNotification") || [];
        history.push(Timestamp.fromMillis(happenedAt));
        tx.set(userRef, { LastNotification: history.slice(-MAX_ALARM_HISTORY) }, { merge: true });
      });
    }

    await snap.ref.update({ notifiedAt: Timestamp.now() });
  }
);

exports.checkDeviceOnline = onSchedule(
  { schedule: "every 1 minutes", timeZone: "Etc/UTC" },
  async () => {
    const cutoff = Timestamp.fromMillis(Date.now() - OFFLINE_AFTER_MS);

    // Devices that went quiet. (Docs with no lastSeen, i.e. old firmware, are not matched.)
    const stale = await db.collection("ALSAlert").where("lastSeen", "<", cutoff).get();
    for (const doc of stale.docs) {
      if (doc.get("offlineNotified") === true) continue;
      const lastSeen = doc.get("lastSeen");
      await sendPush(
        doc.id,
        doc.get("token"),
        "ALS Alert: Device offline",
        "No signal from the device for over a minute. Check its power and Wi-Fi. " +
          "Alarms can't reach your phone until it reconnects.",
        { type: "offline", lastSeen: lastSeen.toMillis() }
      );
      await doc.ref.update({ offlineNotified: true });
    }

    // Devices that were reported offline and are heartbeating again.
    const flagged = await db.collection("ALSAlert").where("offlineNotified", "==", true).get();
    for (const doc of flagged.docs) {
      const lastSeen = doc.get("lastSeen");
      if (!lastSeen || lastSeen.toMillis() < cutoff.toMillis()) continue;
      await sendPush(
        doc.id,
        doc.get("token"),
        "ALS Alert: Device back online",
        "The device is connected again and monitoring.",
        { type: "online" }
      );
      await doc.ref.update({ offlineNotified: false });
    }
  }
);
