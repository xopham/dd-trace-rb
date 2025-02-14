# frozen_string_literal: true

require 'datadog/appsec/spec_helper'
require 'datadog/appsec/context'
require 'datadog/appsec/contrib/rack/gateway/response'
require 'datadog/appsec/contrib/rack/reactive/response'
require 'datadog/appsec/reactive/engine'
require 'datadog/appsec/reactive/shared_examples'

RSpec.describe Datadog::AppSec::Contrib::Rack::Reactive::Response do
  let(:engine) { Datadog::AppSec::Reactive::Engine.new }
  let(:appsec_context) { instance_double(Datadog::AppSec::Context) }
  let(:body) { ['Ok'] }
  let(:headers) { { 'content-type' => 'text/html', 'set-cookie' => 'foo' } }

  let(:response) do
    Datadog::AppSec::Contrib::Rack::Gateway::Response.new(
      body,
      200,
      headers,
      context: appsec_context,
    )
  end

  describe '.publish' do
    it 'propagates response attributes to the engine' do
      expect(engine).to receive(:publish).with('response.status', 200)
      expect(engine).to receive(:publish).with(
        'response.headers',
        headers,
      )
      described_class.publish(engine, response)
    end
  end

  describe '.subscribe' do
    context 'not all addresses have been published' do
      it 'does not call the waf context' do
        expect(engine).to receive(:subscribe).with(
          'response.status',
          'response.headers',
        ).and_call_original
        expect(appsec_context).to_not receive(:run_waf)
        described_class.subscribe(engine, appsec_context)
      end
    end

    context 'waf arguments' do
      before { expect(engine).to receive(:subscribe).and_call_original }

      let(:waf_result) do
        Datadog::AppSec::SecurityEngine::Result::Ok.new(
          events: [], actions: {}, derivatives: {}, timeout: false, duration_ns: 0, duration_ext_ns: 0
        )
      end

      context 'all addresses have been published' do
        let(:expected_waf_arguments) do
          {
            'server.response.status' => '200',
            'server.response.headers' => {
              'content-type' => 'text/html',
              'set-cookie' => 'foo',
            },
            'server.response.headers.no_cookies' => {
              'content-type' => 'text/html',
            },
          }
        end

        it 'does call the waf context with the right arguments' do
          expect(appsec_context).to receive(:run_waf)
            .with(expected_waf_arguments, {}, Datadog.configuration.appsec.waf_timeout)
            .and_return(waf_result)

          described_class.subscribe(engine, appsec_context)
          expect(described_class.publish(engine, response)).to be_nil
        end
      end
    end

    it_behaves_like 'waf result' do
      let(:gateway) { response }
    end
  end
end
