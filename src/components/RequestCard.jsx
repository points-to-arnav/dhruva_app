export function RequestCard({ summary }) {
  return (
    <div className="rounded-2xl border-[1.5px] border-card px-4 py-3.5">
      <div className="text-sm text-muted">Your request</div>
      <div className="mt-1 text-[17px] leading-6">{summary}</div>
    </div>
  );
}
