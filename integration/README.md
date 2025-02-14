# Datadog APM Ruby integration test suite

Integration tests for `datadog` that use a variety of real applications.

## Quickstart

1. Build Docker base images:

    ```bash
    ./script/build-images
    ```

You can specify which ruby version to build using the `-v` option. If you are running on ARM architecture (e.g. mac), include `DOCKER_DEFAULT_PLATFORM=linux/arm64` as a prefix to the script.

2. Choose an application, and follow the instructions in the corresponding `README.md`.

## Demo applications

Ruby demo applications are configured with Datadog APM, which can be used to generate sample traces and profiles. These are used to drive tests in the integration suite.

### Applications

See `README.md` in each directory for more information:

- `apps/opentelemetry`: OpenTelemetry traces
- `apps/rack`: Rack application
- `apps/rails-five`: Rails 5 application
- `apps/rails-six`: Rails 6 application
- `apps/rails-seven`: Rails 7 application
- `apps/rspec`: RSpec test suite (CI)
- `apps/ruby`: Generic Ruby application
- `apps/sinatra2-classic`: Sinatra classic application
- `apps/sinatra2-modular`: Sinatra modular application

### Base images

The `images/` folders hosts some images for Ruby applications.

- `datadog/dd-apm-demo:wrk` / `images/wrk/Dockerfile`: `wrk` load testing application (for generating load)
- `datadog/dd-apm-demo:agent` / `images/agent/Dockerfile`: Datadog agent (with default configuration)
- `datadog/dd-apm-demo:rb-<RUBY_VERSION>` / `images/<RUBY_VERSION>/Dockerfile`: MRI Ruby & `Datadog::DemoEnv` (where `<RUBY_VERSION>` is minor version e.g. `2.7`)

Ruby base images include `Datadog::DemoEnv` and other helpers.

### Running Integration Tests

1. Pick an application to run the tests against:

    ```
    cd apps/rails-seven
    ```

2. Pick a Ruby version and build Docker images:

    ```
    ./script/build-images -v 3.3
    ```

Note: you need to build the images using this command whenever you make
any changes in the source code.

3. Run integration test script:

    ```
    ./script/ci -v 3.3
    ```

### Running Integration Tests Not In Docker

Run the test application manually (in a separate terminal):

```
cd apps/rails-seven
export RUBYOPT=-I../../images/include
# Use local dd-trace-rb tree
export DD_DEMO_ENV_GEM_LOCAL_DATADOG=../../..
export DATABASE_URL=mysql2://user:password@localhost:3306
bundle install
bundle exec rake db:create db:migrate
bundle exec rails server -p 3000
```

Run the tests:

```
cd apps/rails-seven
export RUBYOPT=-I../../images/include
export TEST_INTEGRATION=1 TEST_HOSTNAME=localhost TEST_PORT=3000
bundle exec rspec
```

### Debugging

#### Profiling memory

Create a memory heap dump via:

```sh
# Profile for 5 minutes, dump heap to /data/app/ruby-heap.dump
# Where PID = process ID
bundle exec rbtrace -p PID -e 'Thread.new{GC.start; require "objspace"; ObjectSpace.trace_object_allocations_start; sleep(300); io=File.open("/data/app/ruby-heap.dump", "w"); ObjectSpace.dump_all(output: io); io.close}'
```

Then analyze it using `analyzer.rb` (built into the Ruby base images) with:

```sh
# Group objects by generation
ruby /vendor/dd-demo/datadog/analyzer.rb /data/app/ruby-heap.dump

# List objects in GEN_NUM, group by source location
ruby /vendor/dd-demo/datadog/analyzer.rb /data/app/ruby-heap.dump GEN_NUM

# List objects in all generations, group by source location, descending.
ruby /vendor/dd-demo/datadog/analyzer.rb /data/app/ruby-heap.dump objects

# List objects in all generations, group by source location, descending.
# Up to generation END_GEN.
ruby /vendor/dd-demo/datadog/analyzer.rb /data/app/ruby-heap.dump objects END_GEN

# List objects in all generations, group by source location, descending.
# Between generations START_GEN to END_GEN inclusive.
ruby /vendor/dd-demo/datadog/analyzer.rb /data/app/ruby-heap.dump objects END_GEN START_GEN
```
