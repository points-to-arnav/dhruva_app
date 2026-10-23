import { NEEDS } from '../state/sos';
import { CheckIcon } from './CheckIcon';

export function NeedChips({ selected, onToggle }) {
  return (
    <fieldset>
      <legend className="text-[15px] text-muted">What is happening? Pick all that apply</legend>
      <div className="mt-2 flex flex-wrap gap-2">
        {NEEDS.map((need) => {
          const on = selected.includes(need);
          return (
            <button
              key={need}
              type="button"
              aria-pressed={on}
              onClick={() => onToggle(need)}
              className={`flex h-12 cursor-pointer items-center gap-1.5 rounded-full border-[1.5px] text-[17px] ${
                on ? 'border-ink bg-ink pr-[18px] pl-3.5 font-bold text-bg' : 'border-line px-[18px] text-ink'
              }`}
            >
              {on ? <CheckIcon /> : null}
              {need}
            </button>
          );
        })}
      </div>
    </fieldset>
  );
}
