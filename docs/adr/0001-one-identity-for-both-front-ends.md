# One Identity for both front-ends

While Attached, the Adapter sends every frame with the Kernel identity, including frames
from the Network front-end. While Detached, it uses the Default identity, which starts as
the last Kernel identity and which MQTT can change. We did not give the Network
front-end its own logical address (for example 8, Playback 2): the TV then sees two
devices, and an Active Source from the wrong one switches the TV to the wrong input.
