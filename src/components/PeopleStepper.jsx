import { useState } from 'react';

import { MAX_PEOPLE, MIN_PEOPLE } from '../state/sos';

const roundButton =
  'flex size-12 cursor-pointer items-center justify-center rounded-full border-[1.5px] border-line text-[28px] leading-none disabled:cursor-default disabled:opacity-50';

export function PeopleStepper({ value, onChange }) {
  // What the user is typing, so the field can be empty mid-edit; null when
  // not editing. Committed (and clamped by the parent) on blur.
  const [draft, setDraft] = useState(null);

  const commit = () => {
    if (draft === null) return;
    const parsed = parseInt(draft, 10);
    if (!Number.isNaN(parsed)) onChange(parsed);
    setDraft(null);
  };

  return (
    <div className="flex min-h-12 items-center justify-between">
      <div>
        <div className="text-[17px]">People with me</div>
        <div className="text-[13px] text-muted">Up to {MAX_PEOPLE}</div>
      </div>
      <div className="flex items-center gap-3">
        <button
          type="button"
          aria-label="Fewer people"
          disabled={value <= MIN_PEOPLE}
          onClick={() => onChange(value - 1)}
          className={roundButton}
        >
          −
        </button>
        <input
          type="text"
          inputMode="numeric"
          maxLength={3}
          aria-label="Number of people"
          value={draft ?? String(value)}
          onChange={(e) => setDraft(e.target.value.replace(/[^0-9]/g, ''))}
          onBlur={commit}
          onKeyDown={(e) => e.key === 'Enter' && e.currentTarget.blur()}
          onFocus={(e) => e.currentTarget.select()}
          className="h-12 w-16 rounded-xl border-[1.5px] border-line bg-transparent text-center text-2xl font-bold text-ink outline-none focus:border-ink"
        />
        <button
          type="button"
          aria-label="More people"
          disabled={value >= MAX_PEOPLE}
          onClick={() => onChange(value + 1)}
          className={roundButton}
        >
          +
        </button>
      </div>
    </div>
  );
}
