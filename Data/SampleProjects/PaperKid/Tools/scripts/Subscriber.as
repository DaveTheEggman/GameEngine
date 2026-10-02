// Subscriber - a house that takes the paper. On the delivery zone in front of its porch (a static
// trigger), whose child "Marker" floats above to show the house wants one. The first paper to
// arrive ("PaperArrived", from Paper.as) is a delivery: "Delivered" goes out with the points, the
// marker goes, and the zone takes no more.
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
        self.scene.events.emit("Delivered", value);
        Entity@ marker = self.findChildByName("Marker");
        if (marker !is null && marker.isValid())
        {
            marker.setActive(false);
        }
    }
}
