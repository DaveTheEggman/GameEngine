// Subscriber - a house that wants the paper (P1-4 delivery marking, ZERO native).
//
// The locked marking decision: a subscriber house is simply an entity CARRYING this behavior - its
// presence IS the mark. No tag component, no name lookup. Per-house data = the [metadata] fields
// below. The house also carries an `isTrigger` collider = its DELIVERY ZONE; papers ride their own
// physics GROUP so the trigger fires ONLY for papers (no name-sniffing - the group is the filter).
//
// On the FIRST paper to enter, it emits "Delivered" onto the scene event bus (the Level tier relays
// it to the run bus -> the Game script counts it toward quota + score), then goes inert so one house
// scores once. The auto-aim (P1-5) finds these zones with `scene.physics.nearestOverlap` on the
// zone group, so no self-registration is needed here.

class Subscriber
{
    private Entity@ self;

    // ---- per-house data (inspector-authored) ----
    [10, "Points this delivery is worth"] int value;

    // ---- runtime state (no metadata => not a property) ----
    private bool m_delivered = false;

    Subscriber(Entity@ entity) { @self = entity; }

    // A paper entered the delivery zone. The papers-only physics group guarantees `other` is a paper,
    // so there is nothing to check - just score it, once.
    void onTriggerEnter(Entity@ other)
    {
        if (m_delivered || self is null || !self.isValid())
        {
            return;
        }
        m_delivered = true;
        self.scene.events.emit("Delivered", value);
    }
}
