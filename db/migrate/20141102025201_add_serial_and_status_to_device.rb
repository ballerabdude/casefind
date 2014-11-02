class AddSerialAndStatusToDevice < ActiveRecord::Migration
  def change
    add_column :devices, :serial, :string
    add_column :devices, :status, :integer
  end
end
