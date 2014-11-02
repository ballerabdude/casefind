module Api
  module V1
    class DevicesController < ApplicationController
      respond_to :json

      def index
        @devices = Device.all
        respond_with @devices
      end

      def show
        @device = Device.find(params[:id])
        respond_with @device
      end

      def create
        respond_with Device.create(device_params)
      end

      def update
        respond_with Device.update(params[:id], device_params)
      end

      def destroy
        respond_with Device.destroy(params[:id])
      end

      def get_status
        device = Device.where(serial: params[:serial]).last
        render :json => {status: device.status}
      end

      def get_serial
        device = Device.where(imei: params[:imei]).last
        render :json => {serial: device.serial}
      end

      private
        # Never trust parameters from the scary internet, only allow the white list through.
        def device_params
          params.require(:device).permit(:latitude, :longitude, :serial, :status, :imei)
        end
    end
  end
end
