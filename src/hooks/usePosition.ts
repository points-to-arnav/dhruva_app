import * as Location from 'expo-location';
import { useEffect, useState } from 'react';

import { DEMO_POSITION } from '../config';
import type { Position } from '../serial/protocol';

export type LocationStatus = 'demo' | 'searching' | 'found' | 'unavailable';

// Give up on "searching" after this long and warn the user instead.
const SEARCH_TIMEOUT_MS = 20000;

export function usePosition(): { status: LocationStatus; position: Position | null } {
  const [status, setStatus] = useState<LocationStatus>(DEMO_POSITION ? 'demo' : 'searching');
  const [position, setPosition] = useState<Position | null>(DEMO_POSITION);

  useEffect(() => {
    if (DEMO_POSITION) return;
    let cancelled = false;
    let subscription: Location.LocationSubscription | null = null;
    const timeout = setTimeout(() => {
      if (!cancelled) setStatus((s) => (s === 'searching' ? 'unavailable' : s));
    }, SEARCH_TIMEOUT_MS);

    (async () => {
      try {
        const { granted } = await Location.requestForegroundPermissionsAsync();
        if (cancelled) return;
        if (!granted) {
          setStatus('unavailable');
          return;
        }
        // Keep watching even after the timeout, so a late fix still lands.
        subscription = await Location.watchPositionAsync(
          { accuracy: Location.Accuracy.Balanced, timeInterval: 10000, distanceInterval: 10 },
          ({ coords }) => {
            setPosition({
              latitude: coords.latitude,
              longitude: coords.longitude,
              altitude: coords.altitude ?? 0,
              accuracy: coords.accuracy,
            });
            setStatus('found');
          },
          () => setStatus((s) => (s === 'found' ? s : 'unavailable')),
        );
        if (cancelled) subscription.remove();
      } catch {
        if (!cancelled) setStatus('unavailable');
      }
    })();

    return () => {
      cancelled = true;
      clearTimeout(timeout);
      subscription?.remove();
    };
  }, []);

  return { status, position };
}
