import { useEffect, useState } from 'react';

// The phone's battery percentage (Battery Status API), or null where the
// browser doesn't offer it.
export function useBattery() {
  const [level, setLevel] = useState(null);

  useEffect(() => {
    if (!navigator.getBattery) return;
    let battery = null;
    let cancelled = false;
    const update = () => setLevel(Math.round(battery.level * 100));

    navigator
      .getBattery()
      .then((b) => {
        if (cancelled) return;
        battery = b;
        update();
        b.addEventListener('levelchange', update);
      })
      .catch(() => {});

    return () => {
      cancelled = true;
      battery?.removeEventListener('levelchange', update);
    };
  }, []);

  return level;
}
