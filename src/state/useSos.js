import { createContext, useContext } from 'react';

export const SosContext = createContext(null);

export function useSos() {
  const value = useContext(SosContext);
  if (!value) throw new Error('useSos must be used inside SosProvider');
  return value;
}
