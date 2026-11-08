# Arm Swing Walking: Plan For The Next Attempt

## What The Player Wants

- Swinging the arms in opposite directions walks; swinging faster runs. That is the only rule.
- Swings are never even: one arm moves more than the other, mostly forward and back, partly up and down. Any opposite movement counts.
- Slow swings walk slowly, like a thumbstick pushed a little. Faster swings push further.
- When the swinging stops, the walking stops at once, with no gliding on.

## Why Earlier Attempts Failed

- **Only hand height was measured.** Real swings are mostly forward and back, and both hands rise at the ends of a swing, so most of the movement was lost.
- **Extra rules were invented on top of the request.** Rules such as "clearly opposite", "the weaker arm must move at least a fifth as far", "the first swing must move up or down" and "a swing may last at most a second" were meant to stop aiming or body turns from walking. Real swings broke at least one of them every time, so the add-on never walked.
- **Speed came from whole half-swings.** The push waited for a swing to turn, then held its speed until the next turn. That made walking start late and glide on after the arms stopped.
- **The numbers were guesses.** The speed range and time limits came from imagined swings, never from real hand data.
- **The tests checked those guesses.** They used made-up swings that fit the code, so they passed while real swings failed.
- **The game log was not checked for proof that the code ran.** A test where nothing triggered left no trace, so nobody could tell which part failed.

## How To Build It

Keep the existing parts that already work: reading the hands, pushing the game's left thumbstick, the strength setting and the run button. Only replace the part that turns hand movement into a push.

1. **Each frame, measure how fast each hand moves in 3D**, relative to the head, averaged over a few frames (about 50 ms) so tracking jitter cancels out.
2. **Measure only the opposite part of the movement.** Take the two hand velocities, `L` and `R`. The swing speed is `sqrt(max(0, -dot(L, R)))`.
   - It is large when the hands move in opposite directions.
   - It is zero when they move the same way or one hand is still.
   - Uneven arms still count, at a speed between the two hands.
   - No other condition is added.
3. **Smooth the speed over a short window** (start with about 0.25 s). The hands pause for a moment where every swing turns, and the window bridges that pause. The same window sets how quickly walking stops: within about 0.25 s of stopping the arms.
4. **Map the speed to the stick in a straight line.**
   - Below a small dead zone the push is 0.
   - From there it rises evenly to full forward at a "full speed" value.
   - Then multiply by the strength setting.
5. **Log the swing speed while swinging.** Once per second, write the lowest, average and highest swing speed to the add-on log. Every headset test then shows real numbers, or shows that the code never ran.

Turning the body quickly back and forth also moves the hands in opposite directions, so it may walk. That is accepted: the player asked for no extra rules.

## How To Tune And Test

- **Tune from real data, not guesses.** The first headset test only collects swing speeds from the log: slow swings, fast swings, and stopping. Set the dead zone and full speed from those numbers, so a slow swing pushes a little and a hard swing pushes all the way.
- **Make the automatic tests check only the rule:**
  - Opposite movement walks.
  - Moving the hands the same way, one hand alone, or keeping them still never walks.
  - A faster swing pushes further than a slower one.
  - The push is 0 shortly after the arms stop.
- **Don't write a test that passes only because of a chosen number** (for example "a 0.4 m/s swing gives 0.1–0.25"). Such a test only proves the code matches its own guess.
- **Check the log after every headset test.** If it shows no swing speeds, the code never ran: find out why before changing the swing logic.
- **Use one simple version per test.** If the headset test fails, state that the cause is unknown, read the logged numbers, and change only what they point to.
