# frozen_string_literal: true

require_relative '../tracking'
require_relative '../resource'
require_relative '../event'

module Datadog
  module AppSec
    module Contrib
      module Devise
        module Patcher
          # Hook in devise validate method
          module AuthenticatablePatch
            # rubocop:disable Metrics/MethodLength
            def validate(resource, &block)
              result = super
              return result unless AppSec.enabled?
              return result if @_datadog_skip_track_login_event

              track_user_events_configuration = Datadog.configuration.appsec.track_user_events

              return result unless track_user_events_configuration.enabled

              automated_track_user_events_mode = track_user_events_configuration.mode

              appsec_context = Datadog::AppSec.active_context

              return result unless appsec_context

              devise_resource = resource ? Resource.new(resource) : nil

              event_information = Event.new(devise_resource, automated_track_user_events_mode)

              if result
                if event_information.user_id
                  Datadog.logger.debug { 'User Login Event success' }
                else
                  Datadog.logger.debug { 'User Login Event success, but can\'t extract user ID. Tracking empty event' }
                end

                Tracking.track_login_success(
                  appsec_context.trace,
                  appsec_context.span,
                  user_id: event_information.user_id,
                  **event_information.to_h
                )

                return result
              end

              user_exists = nil

              if resource
                user_exists = true
                Datadog.logger.debug { 'User Login Event failure users exists' }
              else
                user_exists = false
                Datadog.logger.debug { 'User Login Event failure user do not exists' }
              end

              Tracking.track_login_failure(
                appsec_context.trace,
                appsec_context.span,
                user_id: event_information.user_id,
                user_exists: user_exists,
                **event_information.to_h
              )

              result
            end
            # rubocop:enable Metrics/MethodLength
          end
        end
      end
    end
  end
end
