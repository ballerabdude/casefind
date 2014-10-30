json.array!(@devices) do |device|
  json.extract! device, :id, :latitude, :longitude
  json.url device_url(device, format: :json)
end
