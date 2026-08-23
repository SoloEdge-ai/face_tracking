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
        self,
        *,
        missing_frame_threshold: int,
        reacquire_timeout_ms: int,
        selection_max_age_ms: int,
    ) -> None:
        self._lock = threading.Lock()
        self._history: deque[_DetectionSnapshot] = deque()
        self._selected: TargetSelection | None = None
        self._state = TrackingState.NO_TARGET
        self._last_seen_at_unix_ns = 0
        self._lost_at_unix_ns = 0
        self._last_observation: SelectedTargetObservation | None = None
        self._missing_frame_threshold = missing_frame_threshold
        self._missing_frames = 0
        self._last_detection_sequence: int | None = None
        self._reacquire_timeout_ns = reacquire_timeout_ms * 1_000_000
        self._selection_max_age_ns = selection_max_age_ms * 1_000_000

    def observe_detection(
        self, result: DetectionResult, *, received_at_unix_ns: int | None = None
    ) -> SelectedTargetObservation | None:
        now = received_at_unix_ns or time.time_ns()
        with self._lock:
            self._history.append(_DetectionSnapshot(now, result))
            self._prune(now)
            if self._selected and (
                result.source_instance_id != self._selected.source_instance_id
                or result.tracker_instance_id != self._selected.tracker_instance_id
            ):
                return self._clear_locked()
            if not self._selected:
                return None
            if (
                self._last_detection_sequence is not None
                and result.sequence <= self._last_detection_sequence
            ):
                return None
            self._last_detection_sequence = result.sequence
            box = next(
                (item for item in result.boxes if item.track_id == self._selected.track_id), None
            )
            if box is not None:
                self._state = TrackingState.TRACKING
                self._last_seen_at_unix_ns = now
                self._lost_at_unix_ns = 0
                self._missing_frames = 0
                self._last_observation = self._from_detection(result, box, TrackingState.TRACKING)
                return self._last_observation
            if self._state == TrackingState.LOST:
                return self._advance_locked(now)
            self._missing_frames += 1
            next_state = (
                TrackingState.LOST
                if self._missing_frames >= self._missing_frame_threshold
                else TrackingState.MISSING
            )
            if next_state == TrackingState.LOST:
                self._lost_at_unix_ns = now
            self._state = next_state
            self._last_observation = self._from_missing(result, next_state)
            return self._last_observation

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
                self._lost_at_unix_ns = 0
                self._missing_frames = 0
                self._last_detection_sequence = result.sequence
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
                "selected_tracker_instance_id": self._selected.tracker_instance_id
                if self._selected
                else None,
                "missing_frames": self._missing_frames,
            }

    def _advance_locked(self, now: int) -> SelectedTargetObservation | None:
        if not self._selected or not self._last_observation:
            return None
        if (
            self._state == TrackingState.LOST
            and self._lost_at_unix_ns > 0
            and now - self._lost_at_unix_ns >= self._reacquire_timeout_ns
        ):
            return self._clear_locked()
        return None

    def _clear_locked(self) -> SelectedTargetObservation:
        self._selected = None
        self._state = TrackingState.NO_TARGET
        self._last_seen_at_unix_ns = 0
        self._lost_at_unix_ns = 0
        self._missing_frames = 0
        self._last_detection_sequence = None
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

    def _from_missing(
        self, result: DetectionResult, state: TrackingState
    ) -> SelectedTargetObservation:
        assert self._selected is not None
        previous = self._last_observation
        return SelectedTargetObservation(
            result.source_instance_id,
            result.tracker_instance_id,
            result.sequence,
            result.captured_at_unix_ns,
            self._selected.track_id,
            previous.target_center_x if previous else 0,
            previous.target_center_y if previous else 0,
            result.image_width,
            result.image_height,
            state,
        )
