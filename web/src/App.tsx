import { QueryClient, QueryClientProvider } from '@tanstack/react-query';
import { RouterProvider, createRouter } from '@tanstack/react-router';
import { useState } from 'react';

import { routeTree } from './routes';
import { ApiError } from './lib/api';

const router = createRouter({
  routeTree,
  defaultPreload: 'intent',
  scrollRestoration: true,
  defaultStructuralSharing: true,
});

declare module '@tanstack/react-router' {
  interface Register {
    router: typeof router;
  }
}


export function App() {
  const [queryClient] = useState(
    () =>
      new QueryClient({
        defaultOptions: {
          queries: {
            // Config, provider lists and usage are all local-file reads behind a
            // loopback socket; caching briefly avoids a refetch storm when a
            // page mounts several panels that each need the same row.
            staleTime: 10_000,
            gcTime: 5 * 60_000,
            retry: (failureCount, error) => {
              // Never retry a validation or authorization outcome; the request
              // will fail identically forever.
              if (error instanceof ApiError && error.status > 0 && error.status < 500) return false;
              return failureCount < 2;
            },
            refetchOnWindowFocus: false,
          },
          mutations: { retry: false },
        },
      }),
  );

  return (
    <QueryClientProvider client={queryClient}>
      <RouterProvider router={router} />
    </QueryClientProvider>
  );
}