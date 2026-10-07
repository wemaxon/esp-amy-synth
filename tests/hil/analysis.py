"""Small, dependency-free audio checks for hardware tests."""


def peak(samples, start=0, end=None):
    window = samples[start:end]
    return max((abs(value) for value in window), default=0)


def onsets(samples, window=32, ratio=3.0, floor=200):
    """Sample positions where short-term energy jumps: hits and attacks.

    Energy is summed over consecutive windows; an onset is a window whose energy
    exceeds ratio x the previous window and an absolute floor. The returned
    position is the first sample in that window above a quarter of its peak.
    """
    found = []
    previous = None
    for start in range(0, len(samples) - window, window):
        block = samples[start:start + window]
        energy = sum(abs(value) for value in block)
        if previous is not None and energy > ratio * previous and energy > floor * window:
            limit = max(abs(value) for value in block) // 4
            first = next(i for i, value in enumerate(block) if abs(value) >= limit)
            found.append(start + first)
        previous = energy
    return found


def largest_step(samples, start, end):
    """Largest sample-to-sample jump in [start, end): clicks show up as outliers."""
    return max(abs(samples[i] - samples[i - 1]) for i in range(max(start, 1), end))
