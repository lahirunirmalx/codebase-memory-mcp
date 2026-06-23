import { useCallback, useState } from "react";
import type { GraphData } from "../lib/types";

interface UseGraphDataResult {
  data: GraphData | null;
  loading: boolean;
  error: string | null;
  /* Load one hierarchy level of the drill-down explorer. `parent` is a container
   * qualified_name; omit (or pass the project) for the repo root. */
  fetchGraph: (project: string, parent?: string) => void;
}

/* The drill-down explorer fetches aggregated container nodes + weighted
 * super-edges from /api/graph. The backend only ever materializes the current
 * level (a few hundred nodes), so this is memory-safe for any repo size -
 * unlike /api/layout, which tried to lay out the whole graph and OOM'd. */
async function fetchLevel(project: string, parent?: string): Promise<GraphData> {
  const params = new URLSearchParams({ project });
  if (parent) params.set("parent", parent);
  const res = await fetch(`/api/graph?${params}`);

  if (!res.ok) {
    const body = await res.json().catch(() => ({ error: res.statusText }));
    throw new Error(body.error ?? `HTTP ${res.status}`);
  }

  return res.json();
}

export function useGraphData(): UseGraphDataResult {
  const [data, setData] = useState<GraphData | null>(null);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const fetchGraph = useCallback(async (project: string, parent?: string) => {
    setLoading(true);
    setError(null);
    try {
      const result = await fetchLevel(project, parent);
      setData(result);
    } catch (e) {
      setError(e instanceof Error ? e.message : "Failed to load graph");
    } finally {
      setLoading(false);
    }
  }, []);

  return { data, loading, error, fetchGraph };
}
