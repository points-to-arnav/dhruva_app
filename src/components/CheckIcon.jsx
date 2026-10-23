export function CheckIcon({ size = 18, className = 'stroke-bg' }) {
  return (
    <svg width={size} height={size} viewBox="0 0 24 24" aria-hidden="true" className={className}>
      <path d="M5 12.5l4.5 4.5L19 7.5" fill="none" strokeWidth="3" strokeLinecap="round" strokeLinejoin="round" />
    </svg>
  );
}
