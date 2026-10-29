# ALS Alert Cloud Functions

These run in your own Firebase project and send the caregiver's phone a push
notification when:

| Function | When | Notification |
|---|---|---|
| `onDeviceEvent` | The device writes `ALSAlert/{uid}/events/{id}` | Patient alarm, test alarm from the app, or left/right electrode loose |
| `checkDeviceOnline` | Every minute | "Device offline" after 60 s without a heartbeat (worst case about 2 min), then "Device back online" |

Patient alarms are also added to `LastNotification` (last 20) for the app's Home screen.

## Cost

Cloud Functions require the **Blaze** (pay-as-you-go) plan, but one device stays
inside the free allowances: about 45,000 function runs a month (2 million free) and
about 3,000 Firestore reads a day from the offline check (50,000 a day free, shared
with the device's ~17,000 a day). Expect $0 to a few cents a month for storing the function's image.
Set a budget alert (e.g. $1) under Google Cloud console → Billing → Budgets & alerts.

The scheduler job is free for the first 3 jobs per billing account; after that
$0.10/month.

## One-time setup

1. In the [Firebase console](https://console.firebase.google.com), open your project
   and upgrade it to **Blaze** (bottom-left, "Upgrade").
2. Make sure **Firestore** and **Authentication → Email/Password** are enabled.
3. Install [Node.js 22 LTS](https://nodejs.org), then in a terminal:

   ```
   npm install -g firebase-tools
   firebase login
   ```

## Deploy

From the repository root (the folder containing `firebase.json`):

```
cd functions
npm install
cd ..
firebase deploy --only functions,firestore:rules --project YOUR_PROJECT_ID
```

`YOUR_PROJECT_ID` is shown under Project settings → General (for the team's
project it is `alsalert`). The first deploy can take a few minutes and may ask
to enable APIs (Cloud Functions, Cloud Build, Artifact Registry, Cloud Scheduler,
Eventarc); answer yes.

**Firestore location:** the functions run in `us-central1`, which matches the
default Firestore location (`nam5`, United States). If your Firestore database is
somewhere else, change `REGION` at the top of `index.js` to a region in the same
location before deploying (e.g. `europe-west1` for `eur3`).

## Check it works

- Firebase console → Functions should list `onDeviceEvent` and `checkDeviceOnline`.
- Tap "Start Alarm" in the app: the device alarms, and the phone gets
  "ALS Alert: Test alarm".
- Unplug the device: within about 2 minutes the phone gets "Device offline".
- Logs: `firebase functions:log --project YOUR_PROJECT_ID`

If no push arrives, check that `ALSAlert/{uid}` has a `token` field (written by the
app at sign-in) and that notifications are allowed for the app on the phone.

## Security rules

`firestore.rules` (deployed with the command above) lets each account read and
write only its own `ALSAlert/{uid}` document and events. **Deploying it replaces
any rules currently set in the console.**
