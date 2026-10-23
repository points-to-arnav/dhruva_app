export const NEEDS = ['Medical help', 'Trapped', 'Fire', 'Flood', 'Need water or food'] as const;
export type Need = (typeof NEEDS)[number];

export const MIN_PEOPLE = 1;
export const MAX_PEOPLE = 100;

export type SosRequest = { needs: Need[]; people: number };

export function clampPeople(n: number) {
  if (!Number.isFinite(n)) return MIN_PEOPLE;
  return Math.min(MAX_PEOPLE, Math.max(MIN_PEOPLE, Math.round(n)));
}

// "Medical help · Trapped · 3 people", needs in the order they are listed.
export function describeRequest({ needs, people }: SosRequest) {
  const ordered = NEEDS.filter((need) => needs.includes(need));
  return [...ordered, people === 1 ? '1 person' : `${people} people`].join(' · ');
}

export function formatTime(date: Date) {
  const pad = (n: number) => String(n).padStart(2, '0');
  return `${pad(date.getHours())}:${pad(date.getMinutes())}`;
}
