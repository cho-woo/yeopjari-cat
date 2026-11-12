# Animation work

Generate a motion video with the connected Dropshot tools before implementing a new or changed animation. Use the current character images as references so the face, coat, proportions and eye colors stay consistent.

Inspect the generated movement, then connect its poses, timing and contact points to the application's actual physics and renderer. Retain the original video, generation details, selected frames and registration in the repository. Verify the resulting movement through the normal application path before publishing.

Use the immutable Cream and Tuxedo master artwork and the shared reference scale registered in `assets/video-actions/identity-v29.json`. Condition new videos on canonical first and last images; the end image must face the actual intended direction. Do not normalize each action by its own posed height, since that changes head and body proportions between actions. Review all three coats at the same application scale before accepting a new clip. Reject changed faces, ears, markings or proportions and retain unused generations as provenance.

Running reversals must keep their running owner through a planted turn. Do not route intermediate running legs through the seated neutral entrance/outro. Preserve complete action and recovery clocks, and start the quiet interval only after recovery finishes.

Walking must use a genuinely continuous source loop with matching boundary poses. Do not repeat an arbitrary excerpt of a combined stand/walk/sit movie. Keep the horizontal registration independent of changing leg silhouettes, register the painted soles to the physical floor, and calibrate travel against source foot contact. Review the complete loop and its entry/exit through the actual application renderer before publishing.
