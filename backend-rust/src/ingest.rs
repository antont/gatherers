use crate::{app::AppState, protocol::EventEnvelope};

pub async fn handle_ingest_event(state: &AppState, envelope: EventEnvelope) -> Result<(), String> {
    state.apply_event(envelope)
}
