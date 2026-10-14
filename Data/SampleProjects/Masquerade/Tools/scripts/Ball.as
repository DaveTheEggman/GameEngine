// Ball - the round. Four hunters, the player and three bots, each hunting another and hunted by a
// third, among guests who dress as they do. A hunter knows only its target's costume, and several
// guests wear each one.
//
// - The round: each hunter gets a costume (one of its six looks shown) and a target, the others in
//   a ring; `roundSeconds` later the most points wins.
// - A tag ("Tag" from Player.as, or a bot's, here): the nearest person within `tagReach` in front
//   of the tagger. The target: points, and the target respawns elsewhere in a new costume. Anyone
//   else: points lost, and the tagger's mask flares (its Flare light) for `revealSeconds`, there for
//   everyone to see.
// - The bots: guests (Guest.as) that this hunts for. A bot grows suspicious of its target when the
//   target stands out near it (sprinting, its mask flaring, not dancing when the music calls, still
//   walking when everyone gathers) and, sure enough, walks up to it and tags it. Now and then it
//   guesses instead: someone in its target's costume, often wrong, and its mask flares.
// - The game (Masquerade.as) hears what the player needs: "Hud" each quarter second (score,
//   seconds left, the target's costume, the hunter's rank), "Note" when something happens to the
//   player, and "RoundOver" with the scores.
//
// The host decides; the round state is replicated for the network (P4).

const int kHunters = 4;
const int kCostumes = 6;
const int kDanceClip = 3;
const int kWalkClip = 1;
const int kGather = 1;
const int kDanceCue = 2;

// Notes for the game (Masquerade.as): what happened, from the player's side.
const int kNoteGotTarget = 1;  // you tagged your target
const int kNoteMissed = 2;     // you tagged the wrong person: your mask flares
const int kNoteTagged = 3;     // your hunter tagged you: a new costume, somewhere else
const int kNoteFlare = 4;      // someone's mask flares: a hunter, there

class Ball
{
    private Entity@ self;

    [null, "The player (seat 0)"] Entity@ player;
    [null, "Bot 1 (seat 1)"] Entity@ bot1;
    [null, "Bot 2 (seat 2)"] Entity@ bot2;
    [null, "Bot 3 (seat 3)"] Entity@ bot3;
    [null, "The crowd: a child per costume, in the costume order, its guests under it"] Entity@ crowd;
    [null, "The places guests go: its children (where the tagged reappear)"] Entity@ spots;
    [240.0, "Seconds in a round"] float roundSeconds;
    [1.6, "How near a tag reaches (m)"] float tagReach;
    [4.0, "Seconds a wrong tag shows the tagger"] float revealSeconds;
    [100, "Points for tagging your target"] int targetPoints;
    [25, "Points lost for tagging anyone else"] int missPoints;
    [1.7, "Moving faster than this (m/s) is sprinting"] float sprintSpeed;
    [16.0, "How far a bot watches for its target standing out (m)"] float watchRange;
    [20.0, "Shortest wait before a bot guesses (s)"] float guessMin;
    [45.0, "Longest wait before a bot guesses (s)"] float guessMax;
    [15.0, "A bot gives up walking up to someone after this (s)"] float approachGiveUp;

    [0.0, "Seconds left in the round", replicated] float timeLeft;
    [false, "The round is over", replicated] bool over;
    [0, "Seat 0's score", replicated] int score0;
    [0, "Seat 1's score", replicated] int score1;
    [0, "Seat 2's score", replicated] int score2;
    [0, "Seat 3's score", replicated] int score3;

    private array<Entity@> m_hunters;
    private array<array<Entity@>> m_groups; // the guests in each costume
    private array<Entity@> m_spots;
    private array<int> m_costume;   // each hunter's
    private array<int> m_target;    // each hunter's target seat
    private array<float> m_reveal;  // seconds its mask still flares
    private array<Float3> m_lastPos;
    private array<float> m_speed;   // measured over the last half second
    private array<float> m_suspicion; // a bot's, of its target
    private array<float> m_guessIn;
    private array<Entity@> m_chasing; // whom a bot is walking up to
    private array<float> m_chaseFor;
    private array<float> m_orderIn; // until a bot's approach order is refreshed
    private float m_sample = 0.0f;
    private float m_hud = 0.0f;
    private int m_cue = 0;
    private Entity@ m_best;         // a tag's search: the nearest in reach so far
    private float m_bestDistance = 0.0f;

    Ball(Entity@ entity) { @self = entity; }

    void onStart()
    {
        m_hunters.insertLast(player);
        m_hunters.insertLast(bot1);
        m_hunters.insertLast(bot2);
        m_hunters.insertLast(bot3);
        m_groups.resize(kCostumes);
        int k = 0;
        Entity@ group = crowd !is null && crowd.isValid() ? crowd.firstChild() : null;
        while (group !is null && group.isValid() && k < kCostumes)
        {
            Entity@ guest = group.firstChild();
            while (guest !is null && guest.isValid())
            {
                m_groups[k].insertLast(guest);
                @guest = guest.nextSibling();
            }
            ++k;
            @group = group.nextSibling();
        }
        Entity@ spot = spots !is null && spots.isValid() ? spots.firstChild() : null;
        while (spot !is null && spot.isValid())
        {
            m_spots.insertLast(spot);
            @spot = spot.nextSibling();
        }
        m_costume.resize(kHunters);
        m_target.resize(kHunters);
        m_reveal.resize(kHunters);
        m_lastPos.resize(kHunters);
        m_speed.resize(kHunters);
        m_suspicion.resize(kHunters);
        m_guessIn.resize(kHunters);
        m_chasing.resize(kHunters);
        m_chaseFor.resize(kHunters);
        m_orderIn.resize(kHunters);
        if (!Net::isClient())
        {
            startRound();
        }
    }

    private void startRound()
    {
        // The targets: a ring through the seats in a random order.
        array<int> order = {0, 1, 2, 3};
        for (int i = kHunters - 1; i > 0; --i)
        {
            int j = Random::intRange(0, i);
            int t = order[i];
            order[i] = order[j];
            order[j] = t;
        }
        for (int i = 0; i < kHunters; ++i)
        {
            m_target[order[i]] = order[(i + 1) % kHunters];
            dress(i, Random::intRange(0, kCostumes - 1));
            m_reveal[i] = 0.0f;
            m_suspicion[i] = 0.0f;
            m_guessIn[i] = Random::range(guessMin, guessMax);
            @m_chasing[i] = null;
            m_lastPos[i] = m_hunters[i].worldPosition();
        }
        score0 = 0;
        score1 = 0;
        score2 = 0;
        score3 = 0;
        timeLeft = roundSeconds;
        over = false;
    }

    void onCue(int cue) { m_cue = cue; }

    void onTag(int seat)
    {
        if (Net::isClient() || over || seat < 0 || seat >= kHunters)
        {
            return;
        }
        Entity@ tagger = m_hunters[seat];
        Float3 p = tagger.worldPosition();
        Float3 ahead = Quaternion::RotateVector(tagger.worldRotation(), Float3(0.0f, 0.0f, 1.0f));
        @m_best = null;
        m_bestDistance = tagReach;
        for (int c = 0; c < kCostumes; ++c)
        {
            for (uint g = 0; g < m_groups[c].length(); ++g)
            {
                consider(m_groups[c][g], p, ahead);
            }
        }
        for (int h = 0; h < kHunters; ++h)
        {
            if (h != seat)
            {
                consider(m_hunters[h], p, ahead);
            }
        }
        if (m_best !is null)
        {
            resolve(seat, m_best);
        }
    }

    // Someone in reach and in front (or right beside), nearer than the best so far (m_best).
    private void consider(Entity@ person, Float3 p, Float3 ahead)
    {
        Float3 q = person.worldPosition();
        float dx = q.x - p.x;
        float dz = q.z - p.z;
        float distance = Math::Sqrt(dx * dx + dz * dz);
        if (distance >= m_bestDistance)
        {
            return;
        }
        if (distance > 0.7f && (dx * ahead.x + dz * ahead.z) / distance < 0.3f)
        {
            return; // behind or well to the side
        }
        @m_best = person;
        m_bestDistance = distance;
    }

    private void resolve(int seat, Entity@ tagged)
    {
        int victim = seatOf(tagged);
        if (victim == m_target[seat])
        {
            addScore(seat, targetPoints);
            respawn(victim);
            if (seat == 0)
            {
                self.scene.events.emit("Note", kNoteGotTarget);
            }
            if (victim == 0)
            {
                self.scene.events.emit("Note", kNoteTagged);
            }
            return;
        }
        addScore(seat, -missPoints);
        m_reveal[seat] = revealSeconds;
        self.scene.events.emit("Note", seat == 0 ? kNoteMissed : kNoteFlare);
    }

    private void respawn(int seat)
    {
        // Somewhere well away from whoever hunts it, in a costume it was not wearing.
        Entity@ hunter = m_hunters[hunterOf(seat)];
        Float3 at = m_hunters[seat].worldPosition();
        for (int tries = 0; tries < 12 && m_spots.length() > 0; ++tries)
        {
            Float3 candidate = m_spots[Random::intRange(0, int(m_spots.length()) - 1)].worldPosition();
            at = candidate;
            if (flat(candidate, hunter.worldPosition()) > 14.0f)
            {
                break;
            }
        }
        NavAgentComponent::of(m_hunters[seat]).warp(at.x, at.y, at.z);
        dress(seat, (m_costume[seat] + Random::intRange(1, kCostumes - 1)) % kCostumes);
        m_reveal[seat] = 0.0f;
        m_lastPos[seat] = at;
        int hunterSeat = hunterOf(seat);
        m_suspicion[hunterSeat] = 0.0f;
        stopChase(hunterSeat);
        if (seat != 0)
        {
            stopChase(seat);
        }
    }

    // A hunter's costume: the one look of its six shown.
    private void dress(int seat, int costume)
    {
        m_costume[seat] = costume;
        int k = 0;
        Entity@ child = m_hunters[seat].firstChild();
        while (child !is null && child.isValid())
        {
            if (InstanceMemberComponent::of(child) !is null)
            {
                child.setActive(k == costume);
                ++k;
            }
            @child = child.nextSibling();
        }
    }

    void onUpdate(double dt)
    {
        float d = float(dt);
        if (d <= 0.0f)
        {
            return;
        }
        flares(d);
        if (Net::isClient() || over)
        {
            return;
        }
        timeLeft -= d;
        if (timeLeft <= 0.0f)
        {
            timeLeft = 0.0f;
            over = true;
            self.scene.events.emit("RoundOver", Float4(float(score0), float(score1), float(score2), float(score3)));
            return;
        }
        m_sample -= d;
        if (m_sample <= 0.0f)
        {
            m_sample = 0.5f;
            for (int h = 0; h < kHunters; ++h)
            {
                Float3 now = m_hunters[h].worldPosition();
                m_speed[h] = flat(now, m_lastPos[h]) / 0.5f;
                m_lastPos[h] = now;
            }
        }
        for (int b = 1; b < kHunters; ++b)
        {
            hunt(b, d);
        }
        m_hud -= d;
        if (m_hud <= 0.0f)
        {
            m_hud = 0.25f;
            self.scene.events.emit("Hud", Float4(float(score0), timeLeft, float(m_costume[m_target[0]]), float(rank(0))));
        }
    }

    // A flaring mask: its light pulses while the reveal lasts, on every peer.
    private void flares(float d)
    {
        for (int h = 0; h < kHunters && h < int(m_hunters.length()); ++h)
        {
            if (m_reveal[h] > 0.0f)
            {
                m_reveal[h] = Math::Max(m_reveal[h] - d, 0.0f);
            }
            Entity@ flare = m_hunters[h].findChildByName("Flare");
            if (flare !is null && flare.isValid())
            {
                LightComponent@ light = LightComponent::of(flare);
                if (light !is null)
                {
                    light.intensity = m_reveal[h] > 0.0f ? 6.0f + 4.0f * Math::Sin(m_reveal[h] * 12.0f) : 0.0f;
                }
            }
        }
    }

    // One bot's hunt: watch its target for standing out, walk up to it when sure, or guess.
    private void hunt(int b, float d)
    {
        int t = m_target[b];
        Entity@ bot = m_hunters[b];
        Entity@ target = m_hunters[t];
        if (m_chasing[b] !is null)
        {
            m_chaseFor[b] += d;
            float distance = flat(bot.worldPosition(), m_chasing[b].worldPosition());
            if (distance <= tagReach)
            {
                Entity@ tagged = m_chasing[b];
                stopChase(b);
                resolve(b, tagged);
                return;
            }
            if (m_chaseFor[b] > approachGiveUp)
            {
                stopChase(b);
                return;
            }
            m_orderIn[b] -= d;
            if (m_orderIn[b] <= 0.0f)
            {
                m_orderIn[b] = 0.5f;
                bot.send("Approach", m_chasing[b].worldPosition());
            }
            return;
        }
        if (flat(bot.worldPosition(), target.worldPosition()) < watchRange)
        {
            m_suspicion[b] += standingOut(t) * d;
        }
        m_suspicion[b] = Math::Max(m_suspicion[b] - 0.03f * d, 0.0f);
        if (m_suspicion[b] >= 1.0f)
        {
            chase(b, target);
            return;
        }
        m_guessIn[b] -= d;
        if (m_guessIn[b] <= 0.0f)
        {
            m_guessIn[b] = Random::range(guessMin, guessMax);
            Entity@ guess = someoneDressedAs(m_costume[t], b);
            if (guess !is null)
            {
                chase(b, guess);
            }
        }
    }

    // How much a hunter stands out right now (suspicion a second): sprinting, its mask flaring,
    // not dancing when the music calls, walking when everyone gathers.
    private float standingOut(int seat)
    {
        float amount = 0.0f;
        if (m_speed[seat] > sprintSpeed)
        {
            amount += 0.8f;
        }
        if (m_reveal[seat] > 0.0f)
        {
            amount += 2.0f;
        }
        int clip = clipOf(seat);
        if (m_cue == kDanceCue && clip != kDanceClip)
        {
            amount += 0.4f;
        }
        else if (m_cue == kGather && clip == kWalkClip)
        {
            amount += 0.15f;
        }
        return amount;
    }

    // A guess: someone near the bot in the costume, a guest or a hunter (not the bot itself).
    private Entity@ someoneDressedAs(int costume, int b)
    {
        Float3 p = m_hunters[b].worldPosition();
        array<Entity@> near;
        for (uint g = 0; g < m_groups[costume].length(); ++g)
        {
            if (flat(p, m_groups[costume][g].worldPosition()) < watchRange)
            {
                near.insertLast(m_groups[costume][g]);
            }
        }
        for (int h = 0; h < kHunters; ++h)
        {
            if (h != b && m_costume[h] == costume && flat(p, m_hunters[h].worldPosition()) < watchRange)
            {
                near.insertLast(m_hunters[h]);
            }
        }
        return near.length() > 0 ? near[Random::intRange(0, int(near.length()) - 1)] : null;
    }

    private void chase(int b, Entity@ person)
    {
        @m_chasing[b] = person;
        m_chaseFor[b] = 0.0f;
        m_orderIn[b] = 0.0f;
    }

    private void stopChase(int b)
    {
        if (b <= 0 || m_chasing[b] is null)
        {
            return;
        }
        @m_chasing[b] = null;
        m_hunters[b].send("Resume", 0);
    }

    // The clip the hunter's shown look plays.
    private int clipOf(int seat)
    {
        Entity@ child = m_hunters[seat].firstChild();
        while (child !is null && child.isValid())
        {
            if (child.active())
            {
                InstanceMemberComponent@ look = InstanceMemberComponent::of(child);
                if (look !is null)
                {
                    return int(look.clip);
                }
            }
            @child = child.nextSibling();
        }
        return 0;
    }

    private int seatOf(Entity@ person)
    {
        for (int h = 0; h < kHunters; ++h)
        {
            if (m_hunters[h] is person)
            {
                return h;
            }
        }
        return -1;
    }

    private int hunterOf(int seat)
    {
        for (int h = 0; h < kHunters; ++h)
        {
            if (m_target[h] == seat)
            {
                return h;
            }
        }
        return seat;
    }

    private void addScore(int seat, int points)
    {
        if (seat == 0) score0 += points;
        else if (seat == 1) score1 += points;
        else if (seat == 2) score2 += points;
        else score3 += points;
    }

    private int scoreOf(int seat)
    {
        return seat == 0 ? score0 : (seat == 1 ? score1 : (seat == 2 ? score2 : score3));
    }

    // 1 for the lead (ties share it).
    private int rank(int seat)
    {
        int r = 1;
        for (int h = 0; h < kHunters; ++h)
        {
            if (scoreOf(h) > scoreOf(seat))
            {
                ++r;
            }
        }
        return r;
    }

    private float flat(Float3 a, Float3 b)
    {
        float dx = a.x - b.x;
        float dz = a.z - b.z;
        return Math::Sqrt(dx * dx + dz * dz);
    }
}
