# rubocop:disable all
ENV['RAILS_ENV'] = 'production'

require 'bundler/setup'
require 'rails/all'
Bundler.require(*Rails.groups)

module SampleApp
  class Application < Rails::Application; end
end

module OverrideConfiguration
  def paths
    super.tap { |path| path.add 'config/database', with: 'benchmarks/postgres_database.yml' }
  end
end

Rails::Application::Configuration.prepend(OverrideConfiguration)

Rails.application.configure do
  config.cache_classes = true
  config.eager_load = true
  config.active_job.queue_adapter = :sidekiq
end

ActiveRecord::Base.configurations = Rails.application.config.database_configuration
Rails.application.initialize!
# ActiveRecord::Base.connection_config


ActiveRecord::Schema.define do
  drop_table(:samples) if connection.table_exists?(:samples)

  create_table :samples do |t|
    t.string :name
    t.timestamps
  end
end

class Sample < ActiveRecord::Base; end

require 'sidekiq/launcher'
require 'sidekiq/cli'
require 'concurrent/atomic/atomic_fixnum'

Sidekiq.configure_server do |config|
  redis_conn = proc { Redis.new(host: 'localhost', port: 6379) }
  config.redis = ConnectionPool.new(size: 27, timeout: 3, &redis_conn)
end

options = Sidekiq.options.tap do |options|
  options[:tag] = 'test'
  options[:queues] << 'default'
  options[:concurrency] = 20
  options[:timeout] = 2
end

class Worker
  class << self
    attr_reader :iterations, :conditional_variable
  end

  @iterations = Concurrent::AtomicFixnum.new(0)
  @conditional_variable = ConditionVariable.new

  include Sidekiq::Worker
  def perform(iter, max_iterations)
    self.class.iterations.increment
    self.class.conditional_variable.broadcast if self.class.iterations.value > max_iterations

    Sample.create!(name: iter.to_s).save

    100.times do
      Sample.last.name
    end

    Sample.last(100).to_a
  end
end

if Datadog.respond_to?(:configure)
  Datadog.configure do |d|
    d.use :rails,
          enabled: true,
          auto_instrument_redis: true,
          auto_instrument: true,
          tags: { 'tag' => 'value' }

    d.use :http
    d.use :sidekiq, service_name: 'service'
    d.use :redis
    d.use :dalli
    d.use :resque, workers: [Worker]

    processor = Datadog::Pipeline::SpanProcessor.new do |span|
      true if span.service == 'B'
    end

    Datadog::Pipeline.before_flush(processor)
  end
end

def current_memory
  `ps -o rss #{$$}`.split("\n")[1].to_f/1024
end

def time
  Process.clock_gettime(Process::CLOCK_MONOTONIC)
end

def launch(iterations, options)
  iterations.times do |i|
    Worker.perform_async(i, iterations)
  end

  launcher = Sidekiq::Launcher.new(options)
  launcher.run
end

def wait_and_measure(iterations)
  start = time

  STDERR.puts "#{time-start}, #{current_memory}"

  mutex = Mutex.new

  while Worker.iterations.value < iterations
    mutex.synchronize do
      Worker.conditional_variable.wait(mutex, 1)
      STDERR.puts "#{time-start}, #{current_memory}"
    end
  end
end

launch(1000, options)
wait_and_measure(1000)