require 'api_contstraints'

Rails.application.routes.draw do


  namespace :api, defaults: {format: 'json'} do
    scope module: :v1, constraints: ApiConstraints.new(version: 1, default: :true) do
      resources :devices
    end
  end

  root 'devices#index'
  resources :devices, only: [:index, :show]
end
