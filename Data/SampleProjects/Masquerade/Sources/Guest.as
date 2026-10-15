// Guest - one guest of the ball. On a guest's entity, which carries a navigation agent and an
// Instance Member of its costume's instanced mesh (the Guests entity's: one draw for everyone in
// that costume); what the guest is doing is its `activity`, which is also the clip it plays.
//
// The orchestra's cue (the "Cue" event, from Orchestra.as) says what the ball does:
// - Stroll (0): wander from place to place (the children of `spots`), stopping a while at each to
//   stand or to talk; mostly somewhere near, now and then across the ball.
// - Gather (1): go to the nearest place and talk there until the cue changes.
// - Dance (2): dance, there and then, wherever the guest is (the music calls everyone).
//
// A bot hunter is a guest too (Ball.as hunts for it): it wears its costume on a child, one of six
// looks, so the clip goes to whichever it wears, and Ball sends it "Approach" (walk to someone, a
// little brisk) and "Resume" (back to the ball). A guest standing in a player's way steps aside,
// as people at a ball do, then back to its group (walking guests already steer round). `activity` is replicated: the host decides it,
// and every peer's guest plays it.

const int kIdle = 0; // the clips, in the Guests entity's instanced skinning order
const int kWalk = 1;
const int kTalk = 2;
const int kDance = 3;

const int kStroll = 0;
const int kGather = 1;
const int kDanceCue = 2;

class Guest
{
    private Entity@ self;

    [null, "The places guests wander to: its children"] Entity@ spots;
    [null, "The players guests make way for: its children"] Entity@ players;
    [1.3, "How near a player coming at a standing guest makes it step aside (m)"] float makeWayRange;
    [1.0, "Walking speed (m/s): the Walk clip's"] float walkSpeed;
    [2.0, "How far from a place a guest stands (m), so groups spread round it"] float spread;
    [8.0, "Shortest stop at a place (s)"] float stayMin;
    [24.0, "Longest stop at a place (s)"] float stayMax;
    [0.85, "How often the next place is a near one (0 to 1)"] float nearChance;
    [0, "What the guest is doing (0 idle, 1 walk, 2 talk, 3 dance): the clip it plays", replicated] int activity;

    private int m_cue = kStroll;
    private bool m_going = false;
    private float m_stay = 0.0f;
    private int m_settle = kIdle; // what to do on arriving
    private float m_yaw = 0.0f;
    private Float3 m_place = Float3(0.0f, 0.0f, 0.0f); // the middle of where it is going: a group faces it
    private bool m_hasPlace = false;
    private Float3 m_target = Float3(0.0f, 0.0f, 0.0f); // the spot by the place it is walking to
    private Float3 m_last = Float3(0.0f, 0.0f, 0.0f);   // where it was at the last progress check
    private float m_check = 0.0f;                       // until the next progress check
    private float m_walked = 0.0f;                      // seconds on the way
    private array<Entity@> m_spots;
    private bool m_approaching = false; // a bot walking to someone it means to tag
    private bool m_handedOver = false;  // a client's guest, its agent stopped for the host's transforms
    private bool m_aside = false;       // stepping out of a player's way
    private float m_asideFor = 0.0f;
    private int m_asideActivity = 0;    // what it was doing, to go back to
    private float m_wayCheck = 0.0f;    // until the next look for a player coming

    Guest(Entity@ entity) { @self = entity; }

    void onStart()
    {
        gatherChildren(spots, m_spots);
        m_yaw = Random::range(-3.14159f, 3.14159f);
        self.setRotationEuler(0.0f, m_yaw * 57.2957795f, 0.0f);
        // Start mid-evening: some walking, some standing, some talking.
        m_stay = Random::range(0.0f, stayMax);
        activity = Random::boolean() ? kTalk : kIdle;
        m_wayCheck = Random::range(0.0f, 0.25f); // not every guest looks on the same frame
    }

    void onCue(int cue)
    {
        if (cue == m_cue)
        {
            return;
        }
        m_cue = cue;
        if (authority() && !m_approaching)
        {
            // Not all at once: a guest finishes a sentence before following the music.
            m_stay = Random::range(0.0f, 2.5f);
            if (m_going)
            {
                NavAgentComponent::of(self).stop();
                m_going = false;
                activity = kIdle;
            }
        }
    }

    void onUpdate(double dt)
    {
        float d = float(dt);
        if (authority() && d > 0.0f)
        {
            decide(d);
        }
        else if (!m_handedOver)
        {
            // Joined: the host moves this guest now, so its own agent (walking since the scene
            // loaded) stops, or it would fight the replicated transform.
            NavAgentComponent::of(self).stop();
            m_handedOver = true;
        }
        playClip(uint(activity));
        turn(d);
    }

    // A bot's orders (Ball.as). Approach: walk, a little brisk, to the point (where the one it
    // suspects is now; sent again as they move). Resume: back to the ball, soon.
    void onApproach(Float3 at)
    {
        m_approaching = true;
        m_going = false;
        NavAgentComponent::of(self).navigateAt(at.x, at.y, at.z, walkSpeed * 1.3f, 0.3f);
        activity = kWalk;
    }

    void onResume(int unused)
    {
        m_approaching = false;
        NavAgentComponent::of(self).stop();
        activity = kIdle;
        m_stay = Random::range(1.0f, 4.0f);
    }

    // The clip on the guest's own Instance Member, or (a hunter) on each of its looks: only the one
    // it wears is drawn.
    private void playClip(uint clip)
    {
        InstanceMemberComponent@ member = InstanceMemberComponent::of(self);
        if (member !is null)
        {
            member.clip = clip;
            return;
        }
        Entity@ child = self.firstChild();
        while (child !is null && child.isValid())
        {
            InstanceMemberComponent@ look = InstanceMemberComponent::of(child);
            if (look !is null)
            {
                look.clip = clip;
            }
            @child = child.nextSibling();
        }
    }

    private void decide(float d)
    {
        NavAgentComponent@ agent = NavAgentComponent::of(self);
        if (makeWay(agent, d))
        {
            return;
        }
        if (m_approaching)
        {
            if (agent.finished())
            {
                activity = kIdle; // there; Ball decides what comes next
            }
            return;
        }
        if (m_going)
        {
            if (arrived(agent, d))
            {
                agent.stop();
                m_going = false;
                activity = m_settle;
                m_stay = (m_cue == kStroll) ? Random::range(stayMin, stayMax) : 1.0e9f;
            }
            return;
        }
        m_stay -= d;
        if (m_stay > 0.0f)
        {
            return;
        }
        if (m_cue == kDanceCue)
        {
            activity = kDance; // until the music changes
            m_stay = 1.0e9f;
            return;
        }
        // Off to the next place, and what to do there, by the cue.
        if (m_spots.length() == 0)
        {
            m_stay = stayMax;
            return;
        }
        Entity@ place = (m_cue == kGather) ? nearest(m_spots) : pick(m_spots);
        float a = Random::range(-3.14159f, 3.14159f);
        float r = spread * Math::Sqrt(Random::range(0.1f, 1.0f));
        m_place = place.worldPosition();
        m_hasPlace = true;
        Float3 at = m_place + Float3(Math::Sin(a) * r, 0.0f, Math::Cos(a) * r);
        m_target = at;
        m_last = self.worldPosition();
        m_check = 1.0f;
        m_walked = 0.0f;
        if (m_cue == kGather)
        {
            m_settle = kTalk;
        }
        else
        {
            m_settle = Random::range(0.0f, 1.0f) < 0.6f ? kTalk : kIdle;
        }
        agent.navigateAt(at.x, at.y, at.z, walkSpeed, 0.4f);
        activity = kWalk;
        m_going = true;
    }

    // A standing guest a player is coming at steps aside, to the side it already stands on, and
    // after a moment goes back to what it was doing. True while it is out of the way.
    private bool makeWay(NavAgentComponent@ agent, float d)
    {
        if (m_aside)
        {
            m_asideFor -= d;
            if (m_asideFor <= 0.0f)
            {
                m_aside = false;
                agent.stop();
                activity = m_asideActivity;
            }
            return true;
        }
        if (m_going || m_approaching || players is null || !players.isValid())
        {
            return false;
        }
        m_wayCheck -= d;
        if (m_wayCheck > 0.0f)
        {
            return false;
        }
        m_wayCheck = 0.25f;
        Float3 p = self.worldPosition();
        Entity@ player = players.firstChild();
        while (player !is null && player.isValid())
        {
            Float3 q = player.worldPosition();
            float dx = p.x - q.x;
            float dz = p.z - q.z;
            float distance = Math::Sqrt(dx * dx + dz * dz);
            NavAgentComponent@ walker = NavAgentComponent::of(player);
            if (walker !is null && distance < makeWayRange && distance > 0.01f)
            {
                float vx = walker.velocityX();
                float vz = walker.velocityZ();
                float speed = Math::Sqrt(vx * vx + vz * vz);
                // Coming at it: moving, and this guest ahead of it.
                if (speed > 0.3f && (dx * vx + dz * vz) / (distance * speed) > 0.5f)
                {
                    // Out of the line it walks, on the side the guest is already on.
                    float side = (vx * dz - vz * dx) > 0.0f ? 1.0f : -1.0f;
                    float ox = -vz / speed * side;
                    float oz = vx / speed * side;
                    agent.navigateAt(p.x + ox * 0.9f, p.y, p.z + oz * 0.9f, 1.4f, 0.1f);
                    m_asideActivity = activity;
                    activity = kWalk;
                    m_aside = true;
                    m_asideFor = 1.2f;
                    return true;
                }
            }
            @player = player.nextSibling();
        }
        return false;
    }

    // There, or as near as the crowd lets it: a place is a group, and its middle is taken. Close
    // to its spot, or near the place and no longer getting anywhere (the group in the way), or
    // walking far too long (a spot it cannot reach), it stops where it is.
    private bool arrived(NavAgentComponent@ agent, float d)
    {
        m_walked += d;
        if (agent.finished())
        {
            return true;
        }
        Float3 p = self.worldPosition();
        if (flatDistance(p, m_target) < 0.6f || m_walked > 45.0f)
        {
            return true;
        }
        m_check -= d;
        if (m_check > 0.0f)
        {
            return false;
        }
        m_check = 1.0f;
        bool stuck = flatDistance(p, m_last) < 0.3f;
        m_last = p;
        return stuck && flatDistance(p, m_place) < spread + 2.5f;
    }

    // The next place: mostly the nearest of a few picked at random, so most walks are short and
    // the crowd drifts rather than crossing the ball all the time.
    private Entity@ pick(array<Entity@>@ places)
    {
        Entity@ best = places[Random::intRange(0, int(places.length()) - 1)];
        if (Random::range(0.0f, 1.0f) >= nearChance)
        {
            return best;
        }
        Float3 p = self.worldPosition();
        float bestDistance = flatDistance(p, best.worldPosition());
        for (int k = 0; k < 3; ++k)
        {
            Entity@ other = places[Random::intRange(0, int(places.length()) - 1)];
            float distance = flatDistance(p, other.worldPosition());
            if (distance < bestDistance && distance > 1.0f) // not the place it is already at
            {
                @best = other;
                bestDistance = distance;
            }
        }
        return best;
    }

    private Entity@ nearest(array<Entity@>@ places)
    {
        Float3 p = self.worldPosition();
        Entity@ best = places[0];
        float bestDistance = flatDistance(p, best.worldPosition());
        for (uint k = 1; k < places.length(); ++k)
        {
            float distance = flatDistance(p, places[k].worldPosition());
            if (distance < bestDistance)
            {
                @best = places[k];
                bestDistance = distance;
            }
        }
        return best;
    }

    private float flatDistance(Float3 a, Float3 b)
    {
        float dx = a.x - b.x;
        float dz = a.z - b.z;
        return Math::Sqrt(dx * dx + dz * dz);
    }

    // Facing where the crowd moves the guest (its own velocity, not positions differenced: scripts
    // update before the crowd).
    private void turn(float d)
    {
        NavAgentComponent@ agent = NavAgentComponent::of(self);
        if (agent is null || d <= 0.0f)
        {
            return;
        }
        float vx = agent.velocityX();
        float vz = agent.velocityZ();
        if (vx * vx + vz * vz < 0.04f)
        {
            if (!m_hasPlace || activity == kWalk)
            {
                return;
            }
            Float3 p = self.worldPosition(); // standing: face the middle of the group
            vx = m_place.x - p.x;
            vz = m_place.z - p.z;
            if (vx * vx + vz * vz < 0.01f)
            {
                return;
            }
        }
        float want = Math::Atan2(vx, vz); // the model faces +Z
        float delta = want - m_yaw;
        while (delta > 3.14159265f) delta -= 6.2831853f;
        while (delta < -3.14159265f) delta += 6.2831853f;
        m_yaw += delta * Math::Min(6.0f * d, 1.0f);
        self.setRotationEuler(0.0f, m_yaw * 57.2957795f, 0.0f);
    }

    // The host decides; a client's guest plays what it is told (offline is the host).
    private bool authority() { return !Net::isClient(); }

    private void gatherChildren(Entity@ parent, array<Entity@>@ into)
    {
        if (parent is null || !parent.isValid())
        {
            return;
        }
        Entity@ child = parent.firstChild();
        while (child !is null && child.isValid())
        {
            into.insertLast(child);
            @child = child.nextSibling();
        }
    }
}
