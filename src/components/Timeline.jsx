import { CheckIcon } from './CheckIcon';

// steps: [{ title, detail, state: 'done' | 'active' | 'pending' }]
export function Timeline({ steps }) {
  const allDone = steps.every((s) => s.state === 'done');
  return (
    <div className="relative">
      <div className={`absolute top-[18px] bottom-[26px] left-[17px] w-0.5 ${allDone ? 'bg-ink' : 'bg-line'}`} />
      <ol className="flex flex-col gap-6">
        {steps.map((step) => (
          <li key={step.title} className="relative flex items-start gap-4">
            <StepIcon state={step.state} />
            <div className={step.state === 'pending' ? 'text-faint' : ''}>
              <div className="text-[19px] leading-6 font-semibold">{step.title}</div>
              <div className={`mt-0.5 text-[15px] ${step.state === 'pending' ? '' : 'text-muted'}`}>{step.detail}</div>
            </div>
          </li>
        ))}
      </ol>
    </div>
  );
}

function StepIcon({ state }) {
  if (state === 'done') {
    return (
      <span className="flex size-9 flex-none items-center justify-center rounded-full bg-ink">
        <CheckIcon size={20} />
      </span>
    );
  }
  if (state === 'active') {
    return (
      <span className="flex size-9 flex-none items-center justify-center rounded-full border-[2.5px] border-ink bg-bg">
        <span className="size-3 rounded-full bg-ink" />
      </span>
    );
  }
  return <span className="size-9 flex-none rounded-full border-[2.5px] border-line bg-bg" />;
}
