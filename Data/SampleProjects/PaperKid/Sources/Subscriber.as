// Subscriber - a house that takes the paper. On the delivery zone in front of its porch (a static
// trigger), whose child "Marker" floats above to show the house wants one. The first paper to
// arrive ("PaperArrived", from Paper.as) is a delivery: "Delivered" goes out with the points, the
// marker goes, and the zone takes no more. A delivery sounds: the paper slapping the porch and a
// chime.

Guid kLandSound = Guid("5ac330ab-7693-43a0-85ed-7b89d8a484e9");
Guid kDeliveredSound = Guid("0357e902-2dcc-4003-bd4e-340c6f8fb268");

class Subscriber
{
    private Entity@ self;

    [100, "Points for this delivery"] int value;

    private bool m_delivered = false;

    Subscriber(Entity@ entity) { @self = entity; }

    void onPaperArrived(int unused)
    {
        if (m_delivered)
        {
            return;
        }
        m_delivered = true;
        Audio::playOneShot(kLandSound, AudioBus::Effects, 0.9f, Random::range(0.95f, 1.05f));
        Audio::playOneShot(kDeliveredSound, AudioBus::Effects, 0.8f);
        self.scene.events.emit("Delivered", value);
        Entity@ marker = self.findChildByName("Marker");
        if (marker !is null && marker.isValid())
        {
            marker.setActive(false);
        }
    }
}
