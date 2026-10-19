import { clsx, type ClassValue } from 'clsx';
import { twMerge } from 'tailwind-merge';

/** Tailwind-aware class merge, so a component's defaults can be overridden by
 * whatever a caller passes. */
export function cn(...inputs: ClassValue[]): string {
  return twMerge(clsx(inputs));
}