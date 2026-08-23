from __future__ import annotations

import threading
import time
from collections import deque
from dataclasses import dataclass

from .domain import (
    DetectionBox,
    DetectionResult,
    SelectedTargetObservation,
    TrackingState,
)


class TargetSelectionError(ValueError):
    pass


@dataclass(frozen=True, slots=True)
class TargetSelection:
    source_instance_id: str
    tracker_instance_id: str
    sequence: int
    track_id: int


@dataclass(frozen=True, slots=True)
class _DetectionSnapshot:
    received_at_unix_ns: int
    result: DetectionResult


class TargetManager:
    def __init__(
        self, *, lost_after_ms: int, reacquire_timeout_ms: int, selection_max_age_ms: int
    ) -> None:
        self._lock = threading.Lock()
        self._history: deque[_DetectionSnapshot] = deque()
        self._selected: TargetSelection | None = None
        self._state = TrackingState.NO_TARGET
        self._last_seen_at_unix_ns = 0
        self._last_observation: SelectedTargetObservation | None = None
        self._lost_after_ns = lost_after_ms * 1_000_000
        self._reacquire_timeout_ns = reacquire_timeout_ms * 1_000_000
        self._selection_max_age_ns = selection_max_age_ms * 1_000_000

    def observe_detection(
        self, result: DetectionResult, *, received_at_unix_ns: int | None = None
    ) -> SelectedTargetObservation | None:
        now = received_at_unix_ns or time.time_ns()
        with self._lock:
            self._history.append(_DetectionSnapshot(now, result))
            self._prune(now)
            if self._selected and result.tracker_instance_id != self._selected.tracker_instance_id:
                return self._clear_locked()
            if not self._selected:
                return None
            box = next(
                (item for item in result.boxes if item.track_id == self._selected.track_id), None
            )
            if box is not None:
                self._state = TrackingState.TRACKING
                self._last_seen_at_unix_ns = now
                self._last_observation = self._from_detection(result, box, TrackingState.TRACKING)
                return self._last_observation
            return self._advance_locked(now)

    def select(
        self, selection: TargetSelection, *, now_unix_ns: int | None = None
    ) -> SelectedTargetObservation:
        now = now_unix_ns or time.time_ns()
        with self._lock:
            self._prune(now)
            for snapshot in reversed(self._history):
                result = snapshot.result
                if (
                    result.source_instance_id != selection.source_instance_id
                    or result.tracker_instance_id != selection.tracker_instance_id
                    or result.sequence != selection.sequence
                ):
                    continue
                box = next((item for item in result.boxes if item.track_id == selection.track_id), None)
                if box is None or box.track_id == 0:
                    break
                self._selected = selection
                self._state = TrackingState.TRACKING
                self._last_seen_at_unix_ns = snapshot.received_at_unix_ns
                self._last_observation = self._from_detection(result, box, TrackingState.TRACKING)
                return self._last_observation
        raise TargetSelectionError("face selection is missing or stale")

    def cancel(self) -> SelectedTargetObservation:
        with self._lock:
            return self._clear_locked()

    def tick(self, *, now_unix_ns: int | None = None) -> SelectedTargetObservation | None:
        with self._lock:
            return self._advance_locked(now_unix_ns or time.time_ns())

    def snapshot(self) -> dict[str, object]:
        with self._lock:
            return {
                "tracking_state": self._state,
                "selected_track_id": self._selected.track_id if self._selected else None,
                "tracker_instance_id": self._selected.tracker_instance_id if self._selected else None,
            }

    def _advance_locked(self, now: int) -> SelectedTargetObservation | None:
        if not self._selected or not self._last_observation:
            return None
        age = now - self._last_seen_at_unix_ns
        if self._state == TrackingState.TRACKING and age >= self._lost_after_ns:
            self._state = TrackingState.LOST
            self._last_observation = self._replace_state(self._last_observation, TrackingState.LOST)
            return self._last_observation
        if self._state == TrackingState.LOST and age >= self._reacquire_timeout_ns:
            return self._clear_locked()
        return None

    def _clear_locked(self) -> SelectedTargetObservation:
        self._selected = None
        self._state = TrackingState.NO_TARGET
        self._last_seen_at_unix_ns = 0
        self._last_observation = SelectedTargetObservation(
            "", "", 0, 0, 0, 0, 0, 0, 0, TrackingState.NO_TARGET
        )
        return self._last_observation

    def _prune(self, now: int) -> None:
        cutoff = now - self._selection_max_age_ns
        while self._history and self._history[0].received_at_unix_ns < cutoff:
            self._history.popleft()

    @staticmethod
    def _from_detection(
        result: DetectionResult, box: DetectionBox, state: TrackingState
    ) -> SelectedTargetObservation:
        return SelectedTargetObservation(
            result.source_instance_id,
            result.tracker_instance_id,
            result.sequence,
            result.captured_at_unix_ns,
            box.track_id,
            box.x + box.width / 2,
            box.y + box.height / 2,
            result.image_width,
            result.image_height,
            state,
        )

    @staticmethod
    def _replace_state(
        value: SelectedTargetObservation, state: TrackingState
    ) -> SelectedTargetObservation:
        return SelectedTargetObservation(
            value.source_instance_id,
            value.tracker_instance_id,
            value.sequence,
            value.captured_at_unix_ns,
            value.selected_track_id,
            value.target_center_x,
            value.target_center_y,
            value.image_width,
            value.image_height,
            state,
        )
