# frozen_string_literal: true

module Datadog
  module AppSec
    module Contrib
      module Rack
        module Reactive
          # Dispatch data from a Rack request to the WAF context
          module Request
            ADDRESSES = [
              'request.headers',
              'request.uri.raw',
              'request.query',
              'request.cookies',
              'request.client_ip',
              'server.request.method'
            ].freeze
            private_constant :ADDRESSES

            def self.publish(engine, gateway_request)
              catch(:block) do
                engine.publish('request.query', gateway_request.query)
                engine.publish('request.headers', gateway_request.headers)
                engine.publish('request.uri.raw', gateway_request.fullpath)
                engine.publish('request.cookies', gateway_request.cookies)
                engine.publish('request.client_ip', gateway_request.client_ip)
                engine.publish('server.request.method', gateway_request.method)

                nil
              end
            end

            def self.subscribe(engine, context)
              engine.subscribe(*ADDRESSES) do |*values|
                Datadog.logger.debug { "reacted to #{ADDRESSES.inspect}: #{values.inspect}" }

                headers = values[0]
                headers_no_cookies = headers.dup.tap { |h| h.delete('cookie') }
                uri_raw = values[1]
                query = values[2]
                cookies = values[3]
                client_ip = values[4]
                request_method = values[5]

                persistent_data = {
                  'server.request.cookies' => cookies,
                  'server.request.query' => query,
                  'server.request.uri.raw' => uri_raw,
                  'server.request.headers' => headers,
                  'server.request.headers.no_cookies' => headers_no_cookies,
                  'http.client_ip' => client_ip,
                  'server.request.method' => request_method,
                }

                waf_timeout = Datadog.configuration.appsec.waf_timeout
                result = context.run_waf(persistent_data, {}, waf_timeout)

                next unless result.match?

                yield result
                throw(:block, true) unless result.actions.empty?
              end
            end
          end
        end
      end
    end
  end
end
