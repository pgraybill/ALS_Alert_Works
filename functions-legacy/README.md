# Legacy `sendNotification` function

Unmodified copy of the function deployed on the `alsalert` project, downloaded from
the Google Cloud console so it is no longer only in the cloud. Not deployed from here.

It watches `ALSAlert/{documentId}` updates and:

- turns `lastConnectiontemp = "now"` into a `lastConnection` timestamp
- when `notification` changes to `"1"`: resets it to `"0"`, adds the time to the front
  of `LastNotification`, and sends the push

The current functions are in `functions/`; see its README.
