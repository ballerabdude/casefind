class CreateDevices < ActiveRecord::Migration
  def change
    create_table :devices do |t|
      t.decimal :latitude
      t.decimal :longitude

      t.timestamps
    end
  end
end
